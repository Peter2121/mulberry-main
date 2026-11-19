#!/bin/sh
# Update Mulberry timezone database using vzic
# Uses IANA tzdata to generate iCalendar VTIMEZONE .ics files
#
# This script is called during debian package build (debian/rules)
# and can also be run manually for timezone updates.
# POSIX sh: also runs on FreeBSD without bash.

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VZIC_SOURCE_DIR="$PROJECT_ROOT/Libraries/vzic"
VZIC_BUILD_DIR="$PROJECT_ROOT/Libraries/vzic/build"
OUTPUT_DIR="$PROJECT_ROOT/Sources_Common/Timezones/zoneinfo"

echo "=== Mulberry Timezone Database Update ==="
echo "Project root: $PROJECT_ROOT"
echo "vzic source: $VZIC_SOURCE_DIR"
echo "Output directory: $OUTPUT_DIR"

# vzic's Makefile needs GNU make, which is "gmake" on the BSDs
if command -v gmake >/dev/null 2>&1; then
    GNU_MAKE=gmake
else
    GNU_MAKE=make
fi

# Check for required commands
for cmd in $GNU_MAKE pkg-config curl; do
    if ! command -v $cmd >/dev/null 2>&1; then
        echo "ERROR: Required command '$cmd' not found"
        echo "Install it with your package manager (apt install / pkg install)"
        exit 1
    fi
done

# Download latest tzdata source from IANA
echo "Finding latest tzdata version..."
TZDATA_LATEST=$(curl -sL https://data.iana.org/time-zones/releases/ | \
    grep -o 'tzdata202[0-9][a-z]*.tar.gz' | \
    sort -V | tail -1 | sed 's/\.tar\.gz//')

if [ -z "$TZDATA_LATEST" ]; then
    echo "ERROR: Could not determine latest tzdata version"
    exit 1
fi

TZDATA_URL="https://data.iana.org/time-zones/releases/$TZDATA_LATEST.tar.gz"
TZDATA_DIR="/tmp/tzdata-source-$$"
echo "Downloading $TZDATA_LATEST from IANA..."
mkdir -p "$TZDATA_DIR"

if ! curl -sL "$TZDATA_URL" | tar -xz -C "$TZDATA_DIR"; then
    echo "ERROR: Failed to download/extract tzdata source"
    rm -rf "$TZDATA_DIR"
    exit 1
fi

if [ ! -f "$TZDATA_DIR/africa" ]; then
    echo "ERROR: tzdata source files not found after extraction"
    rm -rf "$TZDATA_DIR"
    exit 1
fi

TZDATA_VERSION="${TZDATA_LATEST#tzdata}"
echo "Downloaded tzdata version: $TZDATA_VERSION"

# Check vzic source exists
if [ ! -d "$VZIC_SOURCE_DIR" ]; then
    echo "ERROR: vzic source not found at $VZIC_SOURCE_DIR"
    echo "Run: git submodule update --init Libraries/vzic"
    exit 1
fi

# Build vzic using Makefile
echo "Building vzic..."
cd "$VZIC_SOURCE_DIR"

$GNU_MAKE clean >/dev/null 2>&1 || true
# Use empty TZID_PREFIX to generate standard Olson timezone names (e.g. "America/New_York")
# Use CREATE_SYMLINK=0 so each Link alias (e.g. "Europe/Bratislava") gets its own VTIMEZONE
# with its own TZID - a symlink to the target zone file only carries the target's TZID
$GNU_MAKE TZID_PREFIX="" CREATE_SYMLINK=0 >/dev/null 2>&1

if [ ! -f ./vzic ]; then
    echo "ERROR: vzic build failed"
    exit 1
fi

echo "vzic built successfully"
VZIC_BINARY="$VZIC_SOURCE_DIR/vzic"

# Generate timezone files
echo "Generating timezone .ics files from system tzdata..."
TEMP_OUTPUT="/tmp/mulberry-zoneinfo-$$"
mkdir -p "$TEMP_OUTPUT"

"$VZIC_BINARY" \
    --pure \
    --olson-dir "$TZDATA_DIR" \
    --output-dir "$TEMP_OUTPUT" 2>&1 | grep -v "Modifying RRULE" || true

# Count generated files
ICS_COUNT=$(find "$TEMP_OUTPUT" -name "*.ics" | wc -l)
echo "Generated $ICS_COUNT timezone files"

if [ "$ICS_COUNT" -lt 400 ]; then
    echo "ERROR: Expected at least 400 timezone files, got $ICS_COUNT"
    rm -rf "$TEMP_OUTPUT"
    exit 1
fi

# Backup old files if they exist and we're doing an interactive update
if [ -t 1 ] && [ -d "$OUTPUT_DIR" ] && [ "$(ls -A $OUTPUT_DIR)" ]; then
    BACKUP_FILE="$PROJECT_ROOT/Sources_Common/Timezones/zoneinfo-backup-$(date +%Y%m%d-%H%M%S).tar.gz"
    echo "Backing up old timezone files to $(basename $BACKUP_FILE)..."
    tar czf "$BACKUP_FILE" -C "$PROJECT_ROOT/Sources_Common/Timezones" zoneinfo/
fi

# Replace timezone files
echo "Installing new timezone files..."
mkdir -p "$OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"/*
cp -r "$TEMP_OUTPUT"/* "$OUTPUT_DIR"/

# Create version file
cat > "$OUTPUT_DIR/version.txt" <<EOF
Olson data source: tzdata$TZDATA_VERSION (IANA)
Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Tool: vzic from libical project (https://github.com/libical/vzic)
Downloaded from: $TZDATA_URL
EOF

echo "Created version file:"
cat "$OUTPUT_DIR/version.txt"

# Cleanup
rm -rf "$TEMP_OUTPUT" "$TZDATA_DIR"

# Verify installation
INSTALLED_COUNT=$(find "$OUTPUT_DIR" -name "*.ics" | wc -l)
echo "Installed $INSTALLED_COUNT timezone files to $OUTPUT_DIR"

# Spot check a few common timezones
for tz in America/New_York Europe/London Asia/Tokyo; do
    if [ ! -f "$OUTPUT_DIR/$tz.ics" ]; then
        echo "WARNING: Expected timezone file missing: $tz.ics"
    fi
done

echo "=== Timezone database update complete ==="
