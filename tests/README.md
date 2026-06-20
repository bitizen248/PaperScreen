# Tests

## SDK Host Tests

The SDK tests are plain host-side C++ tests. They do not require the board, Arduino, PlatformIO, or device drivers.

Run:

```sh
./tests/sdk/run_sdk_tests.sh
```

These tests cover the app-facing SDK contract:

- app registry lookup
- capability declarations
- `PaperApp` lifecycle shape
- app event and render results
- first TRMNL `PaperApp` state behavior
- mocked `AppContext` storage, network, clock, battery, power, and log APIs

Firmware service adapters and board drivers still need PlatformIO or device-level verification.
