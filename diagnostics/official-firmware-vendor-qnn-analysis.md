# Official firmware / OTA investigation for NX741J

Investigation date: 2026-07-14.

## Exact installed build

- Product: nubia NX741J / PQ85A01
- Display ID: `MyOS16.0.28_NX741J_NEEA`
- System incremental: `20260605.090729`
- Vendor incremental: `20260605.093157`
- Android 16, security patch 2026-05-01

## Local search

No matching full OTA, factory package, `payload.bin`, `super.img`, or vendor image was already
present in the repository build area or the local downloads directory.

## Official-source search

Checked nubia's official [product download selector](https://www.nubia.com/en/support/select-product.html)
and [download support area](https://www.nubia.com/en/support/download.html). Z80 Ultra is offered as
a selectable product, but no publicly discoverable package matching the build above was available.
Exact searches for the display ID, incremental value, NX741J, and PQ85A01 on official nubia pages
also returned no matching firmware package.

The installed `com.zte.zdm` system-update application was inspected only through normal package
metadata, its readable APK manifest, and public intent declarations. It identifies itself as
`WHK.MFV.FOTA.16.0.000.000.2604021207`, but exposes no ordinary public resource that identifies a
downloadable full package for the installed build. No token, private storage, traffic, or update
server query was inspected.

## Result

An exact official full OTA/factory package could not be obtained. Consequently no firmware image
was extracted and no file was written back to the device. Non-official ROM mirrors and reposted
OTA packages were not consulted.
