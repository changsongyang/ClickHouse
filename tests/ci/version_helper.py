#!/usr/bin/env python3
import os
import subprocess
import datetime

FILE_WITH_VERSION_PATH = "cmake/autogenerated_versions.txt"
CHANGELOG_IN_PATH = "debian/changelog.in"
CHANGELOG_PATH = "debian/changelog"
CONTRIBUTORS_SCRIPT_DIR = "src/Storages/System/"


class ClickHouseVersion():
    def __init__(self, major, minor, patch, tweak, revision):
        self.major = major
        self.minor = minor
        self.patch = patch
        self.tweak = tweak
        self.revision = revision

    def minor_update(self):
        return ClickHouseVersion(
            self.major,
            self.minor + 1,
            1,
            1,
            self.revision + 1)

    def patch_update(self):
        return ClickHouseVersion(
            self.major,
            self.minor,
            self.patch + 1,
            1,
            self.revision)

    def tweak_update(self):
        return ClickHouseVersion(
            self.major,
            self.minor,
            self.patch,
            self.tweak + 1,
            self.revision)

    def get_version_string(self):
        return '.'.join([
            str(self.major),
            str(self.minor),
            str(self.patch),
            str(self.tweak)
        ])

    def as_tuple(self):
        return (self.major, self.minor, self.patch, self.tweak)


class VersionType():
    STABLE = "stable"
    TESTING = "testing"


def build_version_description(version, version_type):
    return "v" + version.get_version_string() + "-" + version_type


def _get_version_from_line(line):
    _, ver_with_bracket = line.strip().split(' ')
    return ver_with_bracket[:-1]

def get_tweak_from_git_describe(repo_path):
    # something like v21.12.1.8816-testing-358-g81942b8128
    # or v21.11.4.14-stable-31-gd6aab025e0
    output = subprocess.check_output(f"cd {repo_path} && git describe --long", shell=True).decode('utf-8')
    commits_number = int(output.split('-')[2])
    # for testing releases we have to also add fourth number of
    # the previous tag
    if 'testing' in output:
        previous_version = output.split('-')[0]
        previous_version_commits = int(previous_version.split('.')[3])
        commits_number += previous_version_commits

    return commits_number


def get_version_from_repo(repo_path):
    path_to_file = os.path.join(repo_path, FILE_WITH_VERSION_PATH)
    major = 0
    minor = 0
    patch = 0
    tweak = get_tweak_from_git_describe(repo_path)
    version_revision = 0
    with open(path_to_file, 'r') as ver_file:
        for line in ver_file:
            if "VERSION_MAJOR" in line and "math" not in line and "SET" in line:
                major = _get_version_from_line(line)
            elif "VERSION_MINOR" in line and "math" not in line and "SET" in line:
                minor = _get_version_from_line(line)
            elif "VERSION_PATCH" in line and "math" not in line and "SET" in line:
                patch = _get_version_from_line(line)
            elif "VERSION_REVISION" in line and "math" not in line:
                version_revision = _get_version_from_line(line)
    return ClickHouseVersion(major, minor, patch, tweak, version_revision)


def _update_cmake_version(repo_path, version, sha, version_type):
    cmd = """sed -i --follow-symlinks -e "s/SET(VERSION_REVISION [^) ]*/SET(VERSION_REVISION {revision}/g;" \
            -e "s/SET(VERSION_DESCRIBE [^) ]*/SET(VERSION_DESCRIBE {version_desc}/g;" \
            -e "s/SET(VERSION_GITHASH [^) ]*/SET(VERSION_GITHASH {sha}/g;" \
            -e "s/SET(VERSION_MAJOR [^) ]*/SET(VERSION_MAJOR {major}/g;" \
            -e "s/SET(VERSION_MINOR [^) ]*/SET(VERSION_MINOR {minor}/g;" \
            -e "s/SET(VERSION_PATCH [^) ]*/SET(VERSION_PATCH {patch}/g;" \
            -e "s/SET(VERSION_STRING [^) ]*/SET(VERSION_STRING {version_string}/g;" \
            {path}""".format(
        revision=version.revision,
        version_desc=build_version_description(version, version_type),
        sha=sha,
        major=version.major,
        minor=version.minor,
        patch=version.patch,
        version_string=version.get_version_string(),
        path=os.path.join(repo_path, FILE_WITH_VERSION_PATH),
    )
    subprocess.check_call(cmd, shell=True)


def _update_changelog(repo_path, version):
    cmd = """sed \
        -e "s/[@]VERSION_STRING[@]/{version_str}/g" \
        -e "s/[@]DATE[@]/{date}/g" \
        -e "s/[@]AUTHOR[@]/clickhouse-release/g" \
        -e "s/[@]EMAIL[@]/clickhouse-release@yandex-team.ru/g" \
        < {in_path} > {changelog_path}
    """.format(
        version_str=version.get_version_string(),
        date=datetime.datetime.now().strftime("%a, %d %b %Y %H:%M:%S") + " +0300",
        in_path=os.path.join(repo_path, CHANGELOG_IN_PATH),
        changelog_path=os.path.join(repo_path, CHANGELOG_PATH)
    )
    subprocess.check_call(cmd, shell=True)

def _update_contributors(repo_path):
    cmd = "cd {} && ./StorageSystemContributors.sh".format(os.path.join(repo_path, CONTRIBUTORS_SCRIPT_DIR))
    subprocess.check_call(cmd, shell=True)

def _update_dockerfile(repo_path, version):
    version_str_for_docker = '.'.join([str(version.major), str(version.minor), str(version.patch), '*'])
    cmd = "ls -1 {path}/docker/*/Dockerfile | xargs sed -i -r -e 's/ARG version=.+$/ARG version='{ver}'/'".format(path=repo_path, ver=version_str_for_docker)
    subprocess.check_call(cmd, shell=True)

def update_version_local(repo_path, sha, version, version_type="testing"):
    _update_contributors(repo_path)
    _update_cmake_version(repo_path, version, sha, version_type)
    _update_changelog(repo_path, version)
    _update_dockerfile(repo_path, version)
