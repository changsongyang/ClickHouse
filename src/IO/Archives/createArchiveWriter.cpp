#include <IO/Archives/LibArchiveWriter.h>
#include <IO/Archives/ZipArchiveWriter.h>
#include <IO/Archives/createArchiveWriter.h>
#include <IO/WriteBuffer.h>
#include <Common/Exception.h>


namespace DB
{
namespace ErrorCodes
{
extern const int CANNOT_PACK_ARCHIVE;
extern const int SUPPORT_IS_DISABLED;
}


std::shared_ptr<IArchiveWriter> createArchiveWriter(const String & path_to_archive)
{
    return createArchiveWriter(path_to_archive, nullptr);
}


std::shared_ptr<IArchiveWriter>
createArchiveWriter(const String & path_to_archive, [[maybe_unused]] std::unique_ptr<WriteBuffer> archive_write_buffer)
{
    if (path_to_archive.ends_with(".zip") || path_to_archive.ends_with(".zipx"))
    {
#if USE_MINIZIP
        return std::make_shared<ZipArchiveWriter>(path_to_archive, std::move(archive_write_buffer));
#else
        throw Exception(ErrorCodes::SUPPORT_IS_DISABLED, "minizip library is disabled");
#endif
    }
    //todo add support for extentsions i.e .gz
    else if (path_to_archive.ends_with(".tar"))
    {
#if USE_LIBARCHIVE
        return std::make_shared<LibArchiveWriter>(path_to_archive, std::move(archive_write_buffer));
#else
        throw Exception(ErrorCodes::SUPPORT_IS_DISABLED, "libarchive library is disabled");
#endif
    }
    else
        throw Exception(ErrorCodes::CANNOT_PACK_ARCHIVE, "Cannot determine the type of archive {}", path_to_archive);
}

}
