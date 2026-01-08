#!/usr/bin/env bash

set -o errexit -o pipefail -o noclobber -o nounset

##########################################################################################
### Define the CLI argument options ######################################################
##########################################################################################

function show_help() {
  echo "This script can be used to..."
  echo " (1) check what the current dev-env is (using --check),"
  echo " (2) download and install the third party libraries from a given specific dev-env (using --install)."
  echo " (3) update the currently used dev-env reference to a new one in all relevant files (using --old-dev-env-update and --new-dev-env-update)."
  echo "usage: dev-env.sh [OPTIONS]"
  echo "Options:"
  echo " -h | --help: Show this help."
  echo " -c | --check: Checks and prints the currently used dev-env."
  echo " -i | --install [current|tag]: Installs the third party libraries of the given dev-env ('current' to simply use the one returned by --check)."
  echo " -o | --old-dev-env-update: The previously used dev-env (to be updated/replaced)."
  echo " -n | --new-dev-env-update: The new dev-env to be used in replacement for the given old dev-env."
}


function exit_with_error() {
  printf "ERROR: %s\n\n" "$@"
  show_help
  exit 1
}

if [ "$#" -eq 0 ]; then
  exit_with_error "No arguments supplied"
fi

LONGOPTS=help,check,install:,old-dev-env-update:,new-dev-env-update:
OPTIONS=hci:o:n:

# -temporarily store output to be able to check for errors
# -activate quoting/enhanced mode (e.g. by writing out “--options”)
# -pass arguments only via   -- "$@"   to separate them correctly
# -if getopt fails, it complains itself to stderr
PARSED=$(getopt --options=$OPTIONS --longoptions=$LONGOPTS --name "$0" -- "$@") || exit 2
# read getopt’s output this way to handle the quoting right:
eval set -- "$PARSED"

##########################################################################################
### Utilities and declaration of env variables ###########################################
##########################################################################################

REPO_ROOT_DIR=$(git rev-parse --show-toplevel)
DEV_ENV_IMAGE_REGISTRY_PREFIX="ghcr.io/celonis/celostar/starrocks-dev-env:"

# These will be filled later during the parsing of CLI args
CURRENT=""
DEV_ENV_TO_INSTALL=""
OLD_DEV_ENV=""
NEW_DEV_ENV=""

function set_and_print_current_dev_env() {
  FILE_TO_EXTRACT_DEV_ENV_FROM="${REPO_ROOT_DIR}/.github/workflows/celonis_presubmit_test.yml"
  echo "Using file '${FILE_TO_EXTRACT_DEV_ENV_FROM}' to extract the currently used dev-env..."
  CURRENT=$(grep "${DEV_ENV_IMAGE_REGISTRY_PREFIX}" "${REPO_ROOT_DIR}/.github/workflows/celonis_presubmit_test.yml" | awk -F'builder_image: ' '{print $2}' | xargs)
  echo "The currently used dev-env is '${CURRENT}'"
}

function check_arg_non_empty() {
  local arg_key=$1
  local arg_value=$2

  if [ -z "${arg_value}" ]; then
    exit_with_error "Value for ${arg_key} must not be empty."
  fi
}

# Stop processing to request user confirmation; only if confirmed (using 'y') will the script continue...
function confirmation() {
  echo "${1}" # Confirmation message as provided by the caller
  echo "!!! Please confirm using 'y' to proceed or anything else to exit..."

  while :; do
    read -r -n 1 key
    if [[ $key != y ]]; then
      echo ""
      echo "Exit."
      exit 0
    else
      echo ""
      echo "Confirmed. Proceeding..."
      break
    fi
  done
}

##########################################################################################
### Parse CLI arguments ##################################################################
##########################################################################################

while true; do
    case "$1" in
        -h | --help)
          show_help;
          exit;
          ;;
        -c | --check)
            set_and_print_current_dev_env
            shift
            ;;
        -i | --install)
            DEV_ENV_TO_INSTALL="$2"
            check_arg_non_empty "--install" "${DEV_ENV_TO_INSTALL}"
            shift 2
            ;;
        -o | --old-dev-env-update)
            OLD_DEV_ENV="$2"
            check_arg_non_empty "--old-dev-env-update" "${OLD_DEV_ENV}"
            shift 2
            ;;
        -n | --new-dev-env-update)
            NEW_DEV_ENV="$2"
            check_arg_non_empty "--new-dev-env-update" "${NEW_DEV_ENV}"
            shift 2
            ;;
        --)
            shift
            break
            ;;
        *)
            exit_with_error "Invalid CLI argument."
    esac
done

OLD_DEV_ENV_SET=0
NEW_DEV_ENV_SET=0
[ -z "${OLD_DEV_ENV}" ] || OLD_DEV_ENV_SET=1
[ -z "${NEW_DEV_ENV}" ] || NEW_DEV_ENV_SET=1

if [ "$OLD_DEV_ENV_SET" -ne "$NEW_DEV_ENV_SET" ]; then
  exit_with_error "To update the dev-env, both --old-dev-env-update=[...] and --new-dev-env-update=[...] must be set."
fi

##########################################################################################
### Conditional Install ##################################################################
##########################################################################################

if [ -n "${DEV_ENV_TO_INSTALL}" ]; then
  if [ "${DEV_ENV_TO_INSTALL}" = "current" ]; then
    if [ -z "${CURRENT}" ]; then
      set_and_print_current_dev_env
    fi
    DEV_ENV_TO_INSTALL=$CURRENT
  fi

  echo "Installing dev-env '${DEV_ENV_TO_INSTALL}'..."

  THIRD_PARTY_DEPS_PATH="/var/local/thirdparty"
  confirmation "If you confirm, this script will (1) pull the docker image '${DEV_ENV_TO_INSTALL}', (2) remove the previously used third party dependencies at '${THIRD_PARTY_DEPS_PATH}' and (3) replace them by the ones in the pulled docker image."
  echo "Pull docker image..."
  docker pull "${DEV_ENV_TO_INSTALL}"
  docker create --name temp-sr-dev-env "${DEV_ENV_TO_INSTALL}"
  echo "Remove existing third party libs..."
  sudo rm -rf "${THIRD_PARTY_DEPS_PATH}"
  echo "Copy third party libs from dev-env..."
  sudo docker cp temp-sr-dev-env:/var/local/thirdparty /var/local/thirdparty
  docker rm temp-sr-dev-env
fi

##########################################################################################
### Conditional Update ###################################################################
##########################################################################################

# Searches for all files containing the old dev-env reference and stores them in the FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN variable
# TODO(n.weber): In the future, if there are use cases for only adjusting some of the found files, we could also add support for
# an exclusion parameter for the script (i.e., --exclude=[list of files]).
function check_for_file_list() {
  IMAGE=${DEV_ENV_IMAGE_REGISTRY_PREFIX}${OLD_DEV_ENV}
  echo "Searching for files containing the old dev-env image reference '${IMAGE}'. This can take a few seconds..."
  readarray -d '' -t FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN < <(grep --null -HRl --exclude-dir={be,fe,lib,log,output,test} "${IMAGE}" "${REPO_ROOT_DIR}")

  if [ ${#FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN[@]} -eq 0 ]; then
    exit_with_error "No files containing the '${IMAGE}' image can be found. Please make sure the provided dev-env name in --old-dev-env is correct."
  else
    echo "Finished. Found a total of ${#FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN[@]} files containing a reference to the given old dev-env '${OLD_DEV_ENV}'. The list of files is as follows:"

    for file_path in "${FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN[@]}"
    do
       printf "\t${file_path}\n"
    done
  fi
}

# Param 1: Path to file where to replace the dev-env in
function replace_dev_env_in_file() {
  local to_replace="${DEV_ENV_IMAGE_REGISTRY_PREFIX}${OLD_DEV_ENV}"
  local replacement="${DEV_ENV_IMAGE_REGISTRY_PREFIX}${NEW_DEV_ENV}"

  echo "Processing '${1}'..."
  # N.B.: We use a custom separator ('|') instead of the sed default '/' because both 'to_replace'/'replacement' contain a '/'
  sed -i "s|${to_replace}|${replacement}|g" "${1}"
}

function replace_dev_env_in_files() {
  for file_path in "${FILES_TO_UPDATE_DEV_ENV_REFERENCE_IN[@]}"
  do
     replace_dev_env_in_file "${file_path}"
  done

  echo "Finished. All occurrences of '${OLD_DEV_ENV}' have successfully been replaced by '${NEW_DEV_ENV}'."
}

if [ "${OLD_DEV_ENV_SET}" -eq 1 ] && [ "${NEW_DEV_ENV_SET}" -eq 1 ]; then
  # 1: Search for all files referencing the old dev-env
  check_for_file_list
  # 2: The script caller needs to confirm that the previously found files should indeed be modified
  confirmation "If you confirm, this script will replace all occurrences of '${DEV_ENV_IMAGE_REGISTRY_PREFIX}${OLD_DEV_ENV}' with '${DEV_ENV_IMAGE_REGISTRY_PREFIX}${NEW_DEV_ENV}' in the above files."
  # 3: Do the replacement
  replace_dev_env_in_files
fi
