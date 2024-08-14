#!/bin/bash
set -e

main() {
  git stash
  git checkout branch-3.2-cb3e808d-celo

  git remote add upstream https://github.com/starrocks/starrocks.git || true

  git fetch upstream ${LATEST_UPSTREAM_BRANCH}:${LATEST_UPSTREAM_BRANCH}
  git push origin ${LATEST_UPSTREAM_BRANCH}

  # get the latest commit
  git fetch
  git checkout ${LATEST_UPSTREAM_BRANCH}
  latest_commit=$(git rev-parse --short=8 HEAD)
  echo "the latest commit is ${latest_commit}"
  # create the new default branch
  newdefaultbranch=${LATEST_UPSTREAM_BRANCH}-${latest_commit}-celo
  echo "The new default branch is: ${newdefaultbranch}"
  echo "new_default_branch=${newdefaultbranch}" >> $GITHUB_ENV
  # check if the branch exists
  if git show-ref --verify --quiet refs/heads/${newdefaultbranch}; then
      echo "branch '${newdefaultbranch}' already exists. Checking out..."
      git checkout ${newdefaultbranch}
  else
      echo "branch '${newdefaultbranch}' does not exist. Creating and checking out and pushing to remote ..."
      git checkout -b ${newdefaultbranch}
      git push --set-upstream origin ${newdefaultbranch}
  fi
  # get the current default branch
  default_branch=$(git remote show origin | grep 'HEAD branch' | sed 's/^[[:space:]]*HEAD branch: //')
  echo "the current default branch is ${default_branch}"

  echo "Start rebasing ..."
  git checkout -B ${default_branch} && \
  timestamp=$(date +'%Y-%m-%dT%H-%M-%S') && \
  testbranch=gh-${LATEST_UPSTREAM_BRANCH}-${latest_commit}-${timestamp} && \
  git checkout -b $testbranch && \
  git rebase ${newdefaultbranch}

  git push --set-upstream origin $testbranch
  echo "test_branch=$testbranch" >> "$GITHUB_OUTPUT"
}

main
