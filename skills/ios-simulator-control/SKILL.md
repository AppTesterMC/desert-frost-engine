---
name: ios-simulator-control
description: Control the iOS 16.4 Simulator (iPhone 14 Pro) to install, launch, capture, and inspect a ScummVM/Dune IPA or simulator .app for engine verification. Use for IPA testing on simulated iOS 16.4 software.
---

# iOS Simulator Control (iPhone 14 Pro, iOS 16.4)

Target runtime: `com.apple.CoreSimulator.SimRuntime.iOS-16-4`
Target device type: `com.apple.CoreSimulator.SimDeviceType.iPhone-14-Pro`

## Ensure the target simulator

```sh
udid=$(xcrun simctl list devices available | awk '/iPhone 14 Pro \(/ {print $NF; exit}' | tr -d '()')
if [ -z "$udid" ]; then
  udid=$(xcrun simctl create "iPhone 14 Pro" \
    com.apple.CoreSimulator.SimDeviceType.iPhone-14-Pro \
    com.apple.CoreSimulator.SimRuntime.iOS-16-4)
fi
xcrun simctl boot "$udid" 2>/dev/null || true
open -a Simulator
```

## Install an IPA or simulator .app

An IPA is a zip. For Simulator, install the contained `.app` (it must be a simulator-arch build; a device-only IPA will not run).

```sh
rm -rf /tmp/dune-ipa && mkdir -p /tmp/dune-ipa
unzip -q /path/to/ScummVM-ios-dune.ipa -d /tmp/dune-ipa
bundle_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
  /tmp/dune-ipa/Payload/ScummVM.app/Info.plist)
xcrun simctl install "$udid" /tmp/dune-ipa/Payload/ScummVM.app
```

## Launch, capture, and inspect

```sh
xcrun simctl launch "$udid" "$bundle_id"
xcrun simctl io "$udid" screenshot /tmp/dune-ios.png
xcrun simctl spawn "$udid" log stream \
  --predicate 'process == "scummvm"' --level debug
```

## Teardown

```sh
xcrun simctl shutdown "$udid"
```

## Notes

- Use only the iOS 16.4 runtime for this target; do not substitute a newer runtime.
- Simulator runs are verification only and do not certify device IPA behavior.
- Never install or launch a device-signed IPA directly on Simulator; extract and install the `.app`.
