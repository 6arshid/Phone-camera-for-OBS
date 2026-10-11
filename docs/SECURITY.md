# Security

What this plugin protects against, what it does not, and how to run it safely.

## Threat model

The intended deployment is a **trusted local network**: your phone and your
computer, on your own Wi-Fi. The design assumes an attacker who can observe and
inject traffic on that network, but not one who controls the OBS machine or the
phone.

Out of scope: an attacker with code execution on either endpoint, physical
access, or a compromised OS.

## What is protected

### The pairing secret never crosses the network

The 32-byte master secret is established out of band — encoded in the QR code
(`ocp1://pair?...&s=<64-hex>&n=<name>`) or typed as a 6-digit code. Only
HMAC-SHA256 proofs derived from it are ever sent. A network observer sees the
proofs but cannot recover the secret.

### Per-session keys

Every connection derives fresh keys via HKDF-SHA256, salted with the client and
server nonces:

| Key | Derivation info | Used for |
| --- | --- | --- |
| `k_auth` | `ocp1-auth` | Authentication proofs only |
| `k_ctrl_enc` / `k_ctrl_mac` | `ocp1-ctrl` | Control channel: ChaCha20-Poly1305-style seal + HMAC |
| `k_media_enc` / `k_media_mac` | `ocp1-media` | Media channel: same construction |

Compromise of one session's keys does not affect any other session, and does not
reveal the master secret.

### Mutual authentication

Both sides prove knowledge of the derived `k_auth` key before anything
sensitive is sent:

* the phone proves first (`phone` role), and
* the OBS side proves back (`obs` role) before the phone will stream.

Each proof is an HMAC over the role, both session nonces and a transcript hash
of the handshake. The two roles bind the nonces in **opposite order** — see
[ARCHITECTURE.md](ARCHITECTURE.md) — which prevents a proof from one role being
replayed as the other.

### Confidentiality and integrity of the stream

Control frames and media packets are encrypted with ChaCha20 and authenticated
with a separate HMAC-SHA256 tag (encrypt-then-MAC), keyed per direction. Packet
counters are bound into the nonce, so replay and reordering within a session are
detected. Media fragments are reassembled only if every fragment authenticates.

### Replay protection across sessions

The two handshake nonces are fresh per connection and are mixed into the key
derivation, so a recorded handshake cannot be replayed to open a new session.

### Pairing is required by default

`require_pairing` defaults to on. An unpaired phone cannot connect; it is
rejected during authentication. *Forget paired devices* clears the stored
secrets.

## What is *not* protected

* **Discovery packets are unencrypted by design.** They carry no secrets, only
  the server's address and ports, and are never trusted for anything beyond
  locating a server. Do not treat a discovered address as authenticated.
* **No protection against an attacker who already has the QR code or pairing
  code.** Anyone who scans the QR or reads the code can pair, for as long as it
  is valid. QR codes and codes expire after 5 minutes — treat them like a
  password and do not screenshot or share them.
* **No end-to-end encryption beyond the phone→OBS hop.** Once decoded, the
  video is ordinary OBS source data and is subject to whatever you do with it
  (streaming, recording, plugins).
* **No anonymity.** Device and host names are transmitted in discovery and
  pairing, and are visible to anyone on the network.
* **No protection on a hostile network beyond the above.** A determined attacker
  can deny service (drop or inject packets), and can attempt offline attacks
  against a captured handshake if they also obtain the master secret.

## Network exposure

The plugin listens on three local ports — TCP 47100 (control), UDP 47101
(media) and UDP 47109 (discovery). See [INSTALL.md](INSTALL.md).

* Scope firewall rules to **Private** networks.
* Do not port-forward these ports to the internet. The protocol is designed for
  a LAN; exposing it publicly invites pairing-code brute force and denial of
  service.
* Discovery announces the machine's presence to the whole LAN. If that is
  undesirable, disable multicast/broadcast for the host or run on an isolated
  network.

## Cryptographic choices

All primitives are implemented in-tree (`src/core/crypto`) against published
test vectors (RFC 8017/4231/5869/8439) rather than pulled from a third-party
library, and are covered by `tests/test_crypto.cpp`.

* **Randomness** comes from the OS CSPRNG — Windows CNG (`BCryptGenRandom`) on
  the plugin, `SecRandomCopyBytes` on iOS, and the platform secure random on
  Android. No user-space PRNG is used for key material.
* **Key stretching** for the 6-digit code path uses PBKDF2-HMAC-SHA256 with a
  minimum of 100000 iterations, so a low-entropy code is not trivially
  brute-forced offline.
* **Constant-time comparison** is used for proof verification.

If you intend to audit or change this code, start with
`src/core/pairing/pairing.cpp` and `tests/test_pairing.cpp`, which pin the exact
key schedule and proof construction.

## Reporting a vulnerability

This project has no bug bounty. If you find a vulnerability, please open a
security advisory or a private issue on the repository rather than a public one,
and include the OBS version, plugin commit, and steps to reproduce.
