# Mess Meal Counter

A lightweight meal-counting app for a shared mess. Members can be added to a live list and marked for morning and night meals. An ESP8266-based OLED display reads the meal totals from Firebase and shows the current period's count.

The web app is a single static HTML file; it has no build step or package manager. The firmware is an Arduino sketch for an ESP8266 and a 1.3-inch SH1106 OLED.

> **Deployment and security status:** This repository contains a functional prototype, not a production-hardened service. The browser client has no sign-in flow and writes directly to Firebase Realtime Database. Do not expose a database with unauthenticated write access to the public internet. Add an authentication and authorization strategy, then verify the database rules before deploying for real users. The firmware also currently embeds Wi-Fi credentials in its source and disables TLS certificate verification; address both before distributing or relying on it in production.

## Features

- Add and remove mess members.
- Toggle each member's morning and night meal status.
- Show live totals in the browser.
- Store member data and aggregate totals in Firebase Realtime Database.
- Display the morning or night total on an SH1106 OLED using an ESP8266.
- Adjust device polling and display power according to the Bangladesh local time schedule.

## Project layout

| Path | Description |
| --- | --- |
| `index.html` | Bengali-language web application and Firebase client. |
| `Farmweare/meal_Counter.ino` | ESP8266 firmware for the OLED meal counter. |
| `CNAME` | Custom-domain value for a static hosting provider that supports this file. |
| `Doc/` | Product and 3D model images. |

## Requirements

### Web app

- A modern browser with JavaScript modules enabled.
- A Firebase project with Realtime Database configured.
- A static web server or hosting provider. Opening `index.html` directly as a `file://` URL may prevent its JavaScript modules from loading.

### OLED counter

- An ESP8266 development board.
- A 1.3-inch SH1106 128×64 I2C OLED display.
- Arduino IDE or another ESP8266-compatible Arduino build/upload environment.
- The ESP8266 Arduino core and the [U8g2 library](https://github.com/olikraus/u8g2).
- Wi-Fi and internet access for NTP time synchronization and Firebase reads.

## Firebase setup

1. Create a Firebase project and enable Realtime Database in the region you intend to use.
2. Configure the Firebase web app settings in `index.html` and the Realtime Database URL in `Farmweare/meal_Counter.ino` to point to that same project.
3. Decide how users and devices will be authenticated and authorized. The current web app does not sign users in, and the firmware makes unauthenticated database reads. Firebase rules must not grant unrestricted public access just to make the app work. A production deployment needs an authentication design and rules that permit only the intended operations.
4. Confirm the configured rules support the required reads and writes, then test with a non-production Firebase project before using real data.

The browser code uses the Firebase JavaScript SDK from Google's CDN. It reads `/members`, writes member records there, and writes aggregate totals to `/summary`. The firmware reads `/summary.json`.

### Database structure

```text
members/
  <push-id>/
    name: "Member name"
    morning: true
    night: true

summary/
  morning: 3
  night: 2
```

`morning` and `night` on each member are booleans. The web app initializes both to `true` when a member is added. It recalculates `/summary` after app-driven additions, toggles, and removals. Because the summary is written by the browser rather than a trusted server, direct database edits or writes from other clients can leave it out of sync. Do not treat it as an authoritative total until summary calculation is moved to a trusted backend or another protected mechanism.

## Run the web app locally

From the repository root, start a local static server:

```sh
python3 -m http.server 8000
```

Open [http://localhost:8000](http://localhost:8000). The page connects to the Firebase project configured in `index.html`; local hosting does not create a local database.

To add a member, enter a name and select **অ্যাড** (Add), or press Enter. Each member starts with both meal toggles enabled. Change a toggle to update the meal count. Select the remove control and confirm to delete a member.

## Deploy the web app

The app is static and can be served by GitHub Pages or another static hosting provider:

1. Configure Firebase and its security model as described above.
2. Publish the repository root, including `index.html`, using the hosting provider's static-site instructions.
3. If using a custom domain, configure its DNS and hosting-provider domain settings. The repository's `CNAME` file contains `meal.xparklab.tech`; the file alone does not configure DNS or enable hosting.
4. Verify the app over HTTPS and test member creation, toggling, removal, and live updates using the intended production authentication and rules.

## Build and upload the OLED firmware

1. Open `Farmweare/meal_Counter.ino` in the Arduino IDE and select the correct ESP8266 board and port.
2. Install the ESP8266 board support package and U8g2 library.
3. Configure the Wi-Fi credentials and Firebase Realtime Database URL for your environment. Keep real credentials out of version control; the current sketch has Wi-Fi credentials embedded directly in the source.
4. Confirm the OLED's I2C wiring and the board's hardware-I2C pins for your specific ESP8266 board.
5. Compile and upload the sketch, then check the OLED and serial output during startup.

The firmware uses NTP to determine Bangladesh time (UTC+6) and follows this schedule:

| Bangladesh time | Mode | Behavior |
| --- | --- | --- |
| 00:00–07:00 | Sleep | OLED and Wi-Fi are off, except for a 30-second display preview after boot. |
| 07:00–11:00 | Active | Wi-Fi remains on and totals are fetched every minute. |
| 11:00–14:00 | Idle | OLED remains on; Wi-Fi is used for an hourly refresh. |
| 14:00–17:00 | Active | Wi-Fi remains on and totals are fetched every minute. |
| 17:00–24:00 | Idle | OLED remains on; Wi-Fi is used for an hourly refresh. |

The OLED shows `SOKAL` before noon and `RAT` from noon to midnight. The device reads the aggregate values from `/summary`; it does not calculate totals from `/members`.

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| The page stays on the connecting status or reports a Firebase error | Check the web app Firebase configuration, Realtime Database URL, browser network access, and database rules. |
| The page loads but member changes fail | Check the database rules and authentication design. Do not solve this by enabling unrestricted public writes. |
| The OLED does not update | Confirm Wi-Fi access, the database URL, the `/summary` values, NTP connectivity, and OLED wiring. The firmware only reads `/summary`. |
| The OLED shows an old total | The firmware refreshes hourly in idle mode and every minute in active mode. Also check whether `/summary` is stale or a Firebase request failed. |
| The page does not load when opened directly | Serve it over HTTP or HTTPS so the browser can load its JavaScript modules. |

## Known limitations

- No application authentication, authorization, input length limits, or administrative roles are implemented.
- Firebase aggregate totals are maintained by the web client and can be stale or manipulated.
- The firmware's HTTPS client calls `setInsecure()`, so it does not validate the Firebase server certificate.
- Wi-Fi credentials are currently embedded in the firmware source.
- The firmware has no visible error indicator for failed Firebase reads; it can continue displaying the last successfully fetched totals.
- The time zone is fixed at UTC+6 and the operating schedule is hard-coded.

Before a production rollout, address these limitations, validate database rules and device access, and test recovery from network and power interruptions.
