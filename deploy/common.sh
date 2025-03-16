#!/bin/bash

# Color definitions
readonly RED='\033[0;31m'
readonly BLUE='\033[0;34m'
readonly BOLD='\033[1m'
readonly NC='\033[0m'

# Print info message
function info() {
    local message=$1
    echo -e "${BLUE}${BOLD}INFO:${NC} ${message}"
}

# Print error message and exit
function error_exit() {
    local message=$1
    echo -e "${RED}${BOLD}ERROR:${NC} $message" >&2
    exit 1
}

# Get the deploy root path
function get_deploy_root() {
    echo "\${HOME}"
}

# Get the remote deploy root path
function get_remote_deploy_root() {
    local deploy_root_pattern
    deploy_root_pattern="$(get_deploy_root)"

    # If it starts with ~/, replace with $HOME for remote expansion
    # shellcheck disable=SC2088
    if [[ "${deploy_root_pattern}" == "~/"* ]]; then
        deploy_root_pattern="\$HOME${deploy_root_pattern#"~"}"
    fi
    readonly deploy_root_pattern

    # Query the expanded path on remote system
    local remote_path
    remote_path=$(ssh "$(get_production_machine)" 'echo '"${deploy_root_pattern}"'') || error_exit "Failed to query remote deploy root"
    echo "${remote_path}"
}

# Check SSH connection
function check_ssh_connection() {
    info "Checking SSH connection to $(get_production_machine)..."

    if ! ssh -q -o BatchMode=yes -o ConnectTimeout=5 "$(get_production_machine)" exit 2>/dev/null; then
        error_exit "Cannot connect to $(get_production_machine). Please check your SSH configuration and try again."
    fi

    info "SSH connection successful"
}
