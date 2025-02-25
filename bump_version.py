import re
from pathlib import Path

from git import GitCommandError, Repo
from semver import Version

MAIN_BRANCH = "main"

VERSION_OCCURANCES = [
    ("README.md", r"(source: github://bernikr/esphome-wienernetze-smartmeter@v)(\S+)()", 1),
    ("components/wienernetze/wienernetze.h", r'(static const char \*WIENERNETZE_VERSION = ")(\S+)(";)', 1),
    ("pyproject.toml", r'(version = ")(\S+)(")', 1),
]

versions = set()
has_wanings = False

for filename, regex, occurrences in VERSION_OCCURANCES:
    with Path(__file__).parent.joinpath(filename).open("r") as f:
        res = re.findall(regex, f.read())
    if len(res) != occurrences:
        print(f"WARNING: version occurance mismatch in {filename}")
        has_wanings = True
    versions.update(r[1] for r in res)

if len(versions) != 1:
    print(f"WARNING: multiple versions found: {versions}")
    has_wanings = True

version = max(Version.parse(v) for v in versions)

if version.prerelease:
    versions = {
        "r": version.finalize_version(),
        "b": version.bump_prerelease(),
    }
else:
    versions = {
        "M": version.bump_major(),
        "m": version.bump_minor(),
        "p": version.bump_patch(),
        "bM": version.bump_major().bump_prerelease("beta"),
        "bm": version.bump_minor().bump_prerelease("beta"),
        "bp": version.bump_patch().bump_prerelease("beta"),
    }

print(f"Current version: {version}")
print("Possible next versions:")
for i, (key, value) in enumerate(versions.items(), start=1):
    print(f" {i} or {key}:{' ' * (4 - len(key))}{value}")

next_version = version.bump_prerelease() if version.prerelease else version.bump_patch()
print(f"Default version: {next_version} [Enter to confirm] or enter a new version")
user_input = input()
if user_input:
    if user_input in versions:
        next_version = versions[user_input]
    elif user_input.isnumeric():
        next_version = list(versions.values())[int(user_input) - 1]
    else:
        next_version = Version.parse(user_input)

print(f"Entered version: {next_version}")
if not next_version > version:
    print("WARNING: new version should be greater than current version")
    has_wanings = True

repo = Repo(Path(__file__).parent)
if repo.is_dirty():
    print("WARNING: repo is dirty, please commit or stage changes before continuing")
    input("Press enter to continue")

for filename, regex, _ in VERSION_OCCURANCES:
    with Path(__file__).parent.joinpath(filename).open("r+") as f:
        res = re.sub(regex, f"\\g<1>{next_version}\\g<3>", f.read())
        f.seek(0)
        f.write(res)
        f.truncate()

if has_wanings:
    print("WARNING: there were warnings, please check the output before contiuning")
    input("Press enter to continue")

res = input("Do you want to commit the changes? [y/N] ")
if res.lower() in {"y", "yes"}:
    repo.git.add(*{filename for filename, _, _ in VERSION_OCCURANCES})
    repo.git.commit("-m", f"Bump version to {next_version}")
    repo.create_tag(f"v{next_version}", message=f"Bump version to {next_version}")
    print("changes committed and created tag")
    if not next_version.prerelease:
        cur_branch = repo.active_branch.name
        repo.git.checkout(MAIN_BRANCH)
        try:
            repo.git.merge(cur_branch, ff_only=True)
        except GitCommandError:
            print(f"WARNING: merge {cur_branch} into {MAIN_BRANCH} failed, please merge manually")
        finally:
            repo.git.checkout(cur_branch)
    res = input("Do you want to push the changes? [y/N] ")
    if res.lower() in {"y", "yes"}:
        repo.git.push(follow_tags=True)
        if not next_version.prerelease:
            repo.git.push("origin", MAIN_BRANCH)
        print("changes pushed")
