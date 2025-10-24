#!/usr/bin/env bash
set -euo pipefail

log() {
	printf '[simtemp-selftest] %s\n' "$*"
}

SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_PATH="${SCRIPT_ROOT}/simtemp.ko"
DTBO_PATH="${SCRIPT_ROOT}/simtemp.dtbo"
APITEST_BIN="${SCRIPT_ROOT}/apitest/apitest"
DEV_NODE="/dev/nxp_simtemp"
OVERLAY_NAME="simtemp"
BOOT_CONFIG_CANDIDATES=("/boot/firmware/config.txt" "/boot/config.txt")

overlay_loaded=false
module_loaded=false
boot_overlay_configured=false

need_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		log "Missing required command: $1"
		exit 1
	}
}

require_file() {
	if [[ ! -f "$1" ]]; then
		log "Required file not found: $1"
		exit 1
	fi
}

ensure_dtbo() {
	if [[ -f "${DTBO_PATH}" ]]; then
		return
	fi
	log "DTBO not found, invoking 'make dtbo'"
	(
		cd "${SCRIPT_ROOT}"
		make dtbo
	)
}

cleanup_overlay() {
	if [[ "${overlay_loaded:-false}" == true ]]; then
		log "Removing overlay ${OVERLAY_NAME}"
		sudo dtoverlay -r "${OVERLAY_NAME}" || true
	fi
}

cleanup_module() {
	if [[ "${module_loaded:-false}" == true ]]; then
		log "Removing module simtemp"
		sudo rmmod simtemp || true
	fi
}

cleanup() {
	cleanup_module
	cleanup_overlay
}

main() {
	need_cmd sudo
	need_cmd dtoverlay
	need_cmd insmod
	need_cmd lsmod
	need_cmd make

	require_file "${MODULE_PATH}"
	ensure_dtbo
	require_file "${DTBO_PATH}"

	if [[ ! -x "${APITEST_BIN}" ]] || [[ "${SCRIPT_ROOT}/apitest/apitest.c" -nt "${APITEST_BIN}" ]]; then
		log "Building apitest utility"
		(
			cd "${SCRIPT_ROOT}"
			make apitest
		)
	fi
	need_cmd "${APITEST_BIN}"

	trap cleanup EXIT

	if lsmod | grep -q '^simtemp'; then
		log "Module already loaded, unloading for a clean start"
		sudo rmmod simtemp
	fi

	for cfg in "${BOOT_CONFIG_CANDIDATES[@]}"; do
		if [[ -f "$cfg" ]] && grep -Eq '^[[:space:]]*dtoverlay=simtemp([[:space:]]|$)' "$cfg"; then
			log "Overlay already configured in ${cfg}; skipping runtime dtoverlay"
			boot_overlay_configured=true
			break
		fi
	done

	if [[ "${boot_overlay_configured}" == false ]]; then
		log "Copying overlay into /boot/overlays/"
		if [[ ! -d /boot/overlays ]]; then
			log "/boot/overlays directory not found; is this running on Raspberry Pi?"
			exit 1
		fi

		sudo cp "${DTBO_PATH}" "/boot/overlays/${OVERLAY_NAME}.dtbo"
		sudo dtoverlay -r "${OVERLAY_NAME}" || true
		if ! sudo dtoverlay "${OVERLAY_NAME}"; then
			log "Failed to apply overlay ${OVERLAY_NAME}"
			exit 1
		fi
		overlay_loaded=true
	else
		log "Ensuring /boot/overlays/${OVERLAY_NAME}.dtbo is up to date"
		if [[ -d /boot/overlays ]]; then
			sudo cp "${DTBO_PATH}" "/boot/overlays/${OVERLAY_NAME}.dtbo"
		fi
	fi

	log "Loading module ${MODULE_PATH}"
	sudo insmod "${MODULE_PATH}"
	module_loaded=true

	log "Waiting for sysfs node availability"
	for i in {1..10}; do
		if [[ -e /sys/class/misc/nxp_simtemp/stats ]]; then
			break
		fi
		sleep 0.2
	done
	if [[ ! -e /sys/class/misc/nxp_simtemp/stats ]]; then
		log "sysfs node not present; aborting"
		exit 1
	fi

	initial_stats=$(</sys/class/misc/nxp_simtemp/stats || true)
	log "Initial stats: ${initial_stats:-unavailable}"

	log "Running CLI self-test"
	"${APITEST_BIN}" "${DEV_NODE}" --test

	final_stats=$(</sys/class/misc/nxp_simtemp/stats || true)
	log "Final stats: ${final_stats:-unavailable}"

	if [[ -n "${initial_stats}" && -n "${final_stats}" ]]; then
		if [[ "${initial_stats}" == "${final_stats}" ]]; then
			log "WARNING: stats did not change; double-check device activity"
		else
			log "Stats updated successfully."
		fi
	fi

	log "Self-test completed successfully"
}

main "$@"
