# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2022 Thomas Basler and others
#
import json
import os

Import("env")

try:
    from dulwich import porcelain
    from dulwich.repo import Repo
except ModuleNotFoundError:
    env.Execute('\"$PYTHONEXE\" -m pip install dulwich')
    from dulwich import porcelain
    from dulwich.repo import Repo


def updateFileIfChanged(filename, content):
    mustUpdate = True
    try:
        with open(filename, "rb") as fp:
            if fp.read() == content:
                mustUpdate = False
    except Exception:
        pass
    if mustUpdate:
        with open(filename, "wb") as fp:
            fp.write(content)
    return mustUpdate


def get_build_version():
    try:
        build_version = porcelain.describe('.')  # '.' refers to the repository root dir
    except Exception:
        build_version = "g0000000"
    print("Firmware Revision: " + build_version)
    return build_version


def get_build_branch():
    # GitHub Actions checks out pull requests as a detached synthetic merge commit.
    # Prefer the workflow-provided source/ref name there, then fall back to the
    # actual local Git branch for normal/local builds.
    branch_name = os.getenv("GITHUB_HEAD_REF") or os.getenv("GITHUB_REF_NAME")
    if not branch_name:
        try:
            branch_name = porcelain.active_branch('.').decode('utf-8')
        except Exception:
            branch_name = "unknown"
    print("Firmware Branch: " + branch_name)
    return branch_name


def get_build_sha():
    build_sha = os.getenv("GITHUB_SHA")
    if not build_sha:
        try:
            build_sha = Repo('.').head().decode('ascii')
        except Exception:
            build_sha = "unknown"
    print("Firmware Commit: " + build_sha)
    return build_sha


def get_update_source(branch_name):
    config_path = os.path.join(env.subst("$PROJECT_DIR"), "firmware-channels.json")
    try:
        with open(config_path, "r", encoding="utf-8") as fp:
            config = json.load(fp)
        repository = config.get("repository", "")
        channel = config.get("channels", {}).get(branch_name, {}).get("id", "")
        if not isinstance(repository, str) or not isinstance(channel, str):
            raise ValueError("invalid firmware channel configuration")
    except Exception as error:
        print("Firmware update source unavailable: " + str(error))
        return "", ""

    if channel:
        print("Firmware Update Channel: " + channel)
    else:
        print("Firmware Update Channel: unpublished branch")
    return repository, channel


def get_firmware_specifier_build_flag():
    build_version = get_build_version()
    build_branch = get_build_branch()
    build_sha = get_build_sha()
    repository, update_channel = get_update_source(build_branch)
    build_flag = "-D AUTO_GIT_HASH=\\\"" + build_version + "\\\""
    build_flag += " -D AUTO_GIT_BRANCH=\\\"" + build_branch + "\\\""
    build_flag += " -D AUTO_GIT_SHA=\\\"" + build_sha + "\\\""
    build_flag += " -D AUTO_UPDATE_REPOSITORY=\\\"" + repository + "\\\""
    build_flag += " -D AUTO_UPDATE_CHANNEL=\\\"" + update_channel + "\\\""
    return build_flag


def do_main():
    if 0:
        # this results in a full recompilation of the whole project after each commit
        env.Append(BUILD_FLAGS=[get_firmware_specifier_build_flag()])
    else:
        # we just create a .c file containing the needed datas
        targetfile = os.path.join(env.subst("$BUILD_DIR"), "__compiled_constants.c")
        lines = ""
        lines += "/* Generated file within build process - Do NOT edit */\n"

        if 0:
            # Add the current date and time as string in UTC timezone
            from datetime import datetime, timezone
            now = datetime.now(tz=timezone.utc)
            COMPILED_DATE_TIME_UTC_STR = now.strftime("%Y/%m/%d %H:%M:%S")
            lines += 'const char *__COMPILED_DATE_TIME_UTC_STR__ = "%s";\n' % (COMPILED_DATE_TIME_UTC_STR)

        if 1:
            build_version = get_build_version()
            build_branch = get_build_branch()
            build_sha = get_build_sha()
            repository, update_channel = get_update_source(build_branch)
            lines += 'const char *__COMPILED_GIT_HASH__ = "%s";\n' % build_version
            lines += 'const char *__COMPILED_GIT_BRANCH__ = "%s";\n' % build_branch
            lines += 'const char *__COMPILED_GIT_SHA__ = "%s";\n' % build_sha
            lines += 'const char *__COMPILED_UPDATE_REPOSITORY__ = "%s";\n' % repository
            lines += 'const char *__COMPILED_UPDATE_CHANNEL__ = "%s";\n' % update_channel

        updateFileIfChanged(targetfile, bytes(lines, "utf-8"))

        # Add the created file to the buildfiles - platformio knows how to handle *.c files
        env.AppendUnique(PIOBUILDFILES=[targetfile])


do_main()
