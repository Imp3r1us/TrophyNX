# Send logs to a computer on your network

The app's debug screen (**X**, then **Y**) sends every TrophyNX log in one go to a small receiver on your network,
for example a Raspberry Pi. No SD card juggling to debug the live module or the hook.

## 1. Start the receiver (once)

Any machine with Python 3.7+ (Raspberry Pi OS has it), standard library only:

```
python3 trophynx_logserver.py            # port 8421, saves to ~/trophynx-logs
```

To keep it running after a reboot on a Raspberry Pi, a user service:

```
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/trophynx-logs.service <<'EOF'
[Unit]
Description=TrophyNX log receiver
[Service]
ExecStart=/usr/bin/python3 %h/trophynx_logserver.py
Restart=always
[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload && systemctl --user enable --now trophynx-logs
sudo loginctl enable-linger $USER      # start it at boot, without logging in
```

## 2. Tell the Switch where it is

Create `sdmc:/switch/TrophyNX/log_server.txt` with one line, the receiver's address: `192.168.1.20` (port 8421) or
`192.168.1.20:9000`. Lines starting with `#` are ignored. This file stays on your SD card: the address is never
part of the app.

## 3. Send

In TrophyNX: **X** (debug screen) then **Y**. A message says how many files went where.

Each upload lands in `~/trophynx-logs/<date_time>/`, and a copy in `~/trophynx-logs/latest/`. The receiver also
serves that folder read-only: `http://<receiver>:8421/latest/live_log.txt` opens in a browser.

What is sent: the data folder's `.txt` / `.json` / `.log` files (live_log, log, close_log, live_save_log,
update_log, definition_errors, popup settings...), `overlay/*.json`, `unlocks/*.json` and the hook manifests
(`atmosphere/contents/*/trophynx-hook.json`), plus a short `info.txt` (app version, time, console address). Never:
`scan_bundle.json` (copies of saves), `keys/` (save passwords), `captures/`. Logs above 1 MB: only their last MB.

The receiver has no password: keep it on your home network, don't forward the port to the internet.
