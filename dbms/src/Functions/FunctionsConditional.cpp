#include <Functions/FunctionsConditional.h>
#include <Functions/FunctionsArray.h>
#include <Functions/FunctionsConversion.h>
#include <Functions/FunctionsTransform.h>
#include <Functions/FunctionFactory.h>
#include <Columns/ColumnNullable.h>

namespace DB
{

void registerFunctionsConditional(FunctionFactory & factory)
{
    factory.registerFunction<FunctionIf>();
    factory.registerFunction<FunctionMultiIf>();
    factory.registerFunction<FunctionCaseWithExpression>();

    /// These are obsolete function names.
    factory.registerFunction("caseWithExpr", FunctionCaseWithExpression::create);
    factory.registerFunction("caseWithoutExpr", FunctionMultiIf::create);
    factory.registerFunction("caseWithoutExpression", FunctionMultiIf::create);
}


/// Implementation of FunctionMultiIf.

FunctionPtr FunctionMultiIf::create(const Context & context)
{
    return std::make_shared<FunctionMultiIf>(context);
}

String FunctionMultiIf::getName() const
{
    return name;
}


static ColumnPtr castColumn(const ColumnWithTypeAndName & arg, const DataTypePtr & type, const Context & context)
{
    Block temporary_block
    {
        arg,
        {
            DataTypeString().createConstColumn(arg.column->size(), type->getName()),
            std::make_shared<DataTypeString>(),
            ""
        },
        {
            nullptr,
            type,
            ""
        }
    };

    FunctionCast func_cast(context);

    {
        DataTypePtr unused_return_type;
        ColumnsWithTypeAndName arguments{ temporary_block.getByPosition(0), temporary_block.getByPosition(1) };
        std::vector<ExpressionAction> unused_prerequisites;

        /// Prepares function to execution. TODO It is not obvious.
        func_cast.getReturnTypeAndPrerequisites(arguments, unused_return_type, unused_prerequisites);
    }

    func_cast.execute(temporary_block, {0, 1}, 2);
    return temporary_block.getByPosition(2).column;
}


void FunctionMultiIf::executeImpl(Block & block, const ColumnNumbers & args, size_t result)
{
    struct Instruction
    {
        const IColumn * condition = nullptr;
        const IColumn * source = nullptr;

        bool condition_always_true = false;
        bool condition_is_nullable = false;
        bool source_is_constant = false;
    };

    std::vector<Instruction> instructions;
    instructions.reserve(args.size() / 2 + 1);

    Columns converted_columns_holder;
    converted_columns_holder.reserve(instructions.size());

    const DataTypePtr & return_type = block.getByPosition(result).type;

    for (size_t i = 0; i < args.size(); i += 2)
    {
        Instruction instruction;
        size_t source_idx = i + 1;

        if (source_idx == args.size())
        {
            /// The last, "else" branch can be treated as a branch with always true condition "else if (true)".
            --source_idx;
            instruction.condition_always_true = true;
        }
        else
        {
            const ColumnWithTypeAndName & cond_col = block.getByPosition(i);

            if (cond_col.column->isNull())
                continue;

            if (cond_col.column->isConst())
            {
                Field value = typeid_cast<const ColumnConst &>(*cond_col.column).getField();
                if (value.isNull())
                    continue;
                if (value.get<UInt64>() == 0)
                    continue;
                instruction.condition_always_true = true;
            }
            else
            {
                if (cond_col.column->isNullable())
                    instruction.condition_is_nullable = true;

                instruction.condition = cond_col.column.get();
            }
        }

        const ColumnWithTypeAndName & source_col = block.getByPosition(source_idx);
        if (source_col.type->equals(*return_type))
        {
            instruction.source = source_col.column.get();
        }
        else
        {
            converted_columns_holder.emplace_back(castColumn(source_col, return_type, context));
            instruction.source = converted_columns_holder.back().get();
        }

        if (instruction.source && instruction.source->isConst())
            instruction.source_is_constant = true;

        instructions.emplace_back(std::move(instruction));

        if (instructions.back().condition_always_true)
            break;
    }

    size_t rows = block.rows();
    ColumnPtr res = return_type->createColumn();

    for (size_t i = 0; i < rows; ++i)
    {
        for (const auto & instruction : instructions)
        {
            bool insert = false;

            if (instruction.condition_always_true)
                insert = true;
            else if (!instruction.condition_is_nullable)
                insert = static_cast<const ColumnUInt8 &>(*instruction.condition).getData()[i];
            else
            {
                const ColumnNullable & condition_nullable = static_cast<const ColumnNullable &>(*instruction.condition);
                const ColumnUInt8 & condition_nested = static_cast<const ColumnUInt8 &>(*condition_nullable.getNestedColumn());
                const NullMap & condition_null_map = condition_nullable.getNullMap();

                insert = !condition_null_map[i] && condition_nested.getData()[i];
            }

            if (insert)
            {
                if (!instruction.source_is_constant)
                    res->insertFrom(*instruction.source, i);
                else
                    res->insertFrom(static_cast<const ColumnConst &>(*instruction.source).getDataColumn(), 0);

                break;
            }
        }
    }

    block.getByPosition(result).column = std::move(res);
}

DataTypePtr FunctionMultiIf::getReturnTypeImpl(const DataTypes & args) const
{
    /// Arguments are the following: cond1, then1, cond2, then2, ... condN, thenN, else.

    auto for_conditions = [&args](auto && f)
    {
        size_t conditions_end = args.size() - 1;
        for (size_t i = 0; i < conditions_end; i += 2)
            f(args[i]);
    };

    auto for_branches = [&args](auto && f)
    {
        size_t branches_end = args.size();
        for (size_t i = 1; i < branches_end; i += 2)
            f(args[i]);
        f(args.back());
    };

    if (!(args.size() >= 3 && args.size() % 2 == 1))
        throw Exception{"Invalid number of arguments for function " + getName(),
            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH};

    /// Conditions must be UInt8, Nullable(UInt8) or Null. If one of conditions is Nullable, the result is also Nullable.
    bool have_nullable_condition = false;
    bool all_conditions_are_null = true;

    for_conditions([&](const DataTypePtr & arg)
    {
        const IDataType * observed_type;
        if (arg->isNullable())
        {
            have_nullable_condition = true;
            all_conditions_are_null = false;
            const DataTypeNullable & nullable_type = static_cast<const DataTypeNullable &>(*arg);
            observed_type = nullable_type.getNestedType().get();
        }
        else if (arg->isNull())
        {
            have_nullable_condition = true;
        }
        else
        {
            all_conditions_are_null = false;
            observed_type = arg.get();
        }

        if (!checkDataType<DataTypeUInt8>(observed_type) && !observed_type->isNull())
            throw Exception{"Illegal type of argument (condition) "
                "of function " + getName() + ". Must be UInt8.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
    });

    if (all_conditions_are_null)
        return std::make_shared<DataTypeNull>();

    DataTypes types_of_branches;
    types_of_branches.reserve(args.size() / 2 + 1);

    for_branches([&](const DataTypePtr & arg)
    {
        types_of_branches.emplace_back(arg);
    });

    DataTypePtr common_type_of_branches = getLeastCommonType(types_of_branches);

    return have_nullable_condition
        ? makeNullableDataTypeIfNot(common_type_of_branches)
        : common_type_of_branches;
}


FunctionPtr FunctionCaseWithExpression::create(const Context & context_)
{
    return std::make_shared<FunctionCaseWithExpression>(context_);
}

FunctionCaseWithExpression::FunctionCaseWithExpression(const Context & context_)
    : context{context_}
{
}

String FunctionCaseWithExpression::getName() const
{
    return name;
}

DataTypePtr FunctionCaseWithExpression::getReturnTypeImpl(const DataTypes & args) const
{
    /// See the comments in executeImpl() to understand why we actually have to
    /// get the return type of a transform function.

    /// Get the return types of the arrays that we pass to the transform function.
    DataTypes src_array_types;
    DataTypes dst_array_types;

    for (size_t i = 1; i < (args.size() - 1); ++i)
    {
        if ((i % 2) != 0)
            src_array_types.push_back(args[i]);
        else
            dst_array_types.push_back(args[i]);
    }

    FunctionArray fun_array{context};

    DataTypePtr src_array_type = fun_array.getReturnTypeImpl(src_array_types);
    DataTypePtr dst_array_type = fun_array.getReturnTypeImpl(dst_array_types);

    /// Finally get the return type of the transform function.
    FunctionTransform fun_transform;
    return fun_transform.getReturnTypeImpl({args.front(), src_array_type, dst_array_type, args.back()});
}

void FunctionCaseWithExpression::executeImpl(Block & block, const ColumnNumbers & args, size_t result)
{
    /// In the following code, we turn the construction:
    /// CASE expr WHEN val[0] THEN branch[0] ... WHEN val[N-1] then branch[N-1] ELSE branchN
    /// into the construction transform(expr, src, dest, branchN)
    /// where:
    /// src  = [val[0], val[1], ..., val[N-1]]
    /// dest = [branch[0], ..., branch[N-1]]
    /// then we perform it.

    /// Create the arrays required by the transform function.
    ColumnNumbers src_array_args;
    DataTypes src_array_types;

    ColumnNumbers dst_array_args;
    DataTypes dst_array_types;

    for (size_t i = 1; i < (args.size() - 1); ++i)
    {
        if ((i % 2) != 0)
        {
            src_array_args.push_back(args[i]);
            src_array_types.push_back(block.getByPosition(args[i]).type);
        }
        else
        {
            dst_array_args.push_back(args[i]);
            dst_array_types.push_back(block.getByPosition(args[i]).type);
        }
    }

    FunctionArray fun_array{context};

    DataTypePtr src_array_type = fun_array.getReturnTypeImpl(src_array_types);
    DataTypePtr dst_array_type = fun_array.getReturnTypeImpl(dst_array_types);

    Block temp_block = block;

    size_t src_array_pos = temp_block.columns();
    temp_block.insert({nullptr, src_array_type, ""});

    size_t dst_array_pos = temp_block.columns();
    temp_block.insert({nullptr, dst_array_type, ""});

    fun_array.executeImpl(temp_block, src_array_args, src_array_pos);
    fun_array.executeImpl(temp_block, dst_array_args, dst_array_pos);

    /// Execute transform.
    FunctionTransform fun_transform;

    ColumnNumbers transform_args{args.front(), src_array_pos, dst_array_pos, args.back()};
    fun_transform.executeImpl(temp_block, transform_args, result);

    /// Put the result into the original block.
    block.getByPosition(result).column = std::move(temp_block.getByPosition(result).column);
}

}
