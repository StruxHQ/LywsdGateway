---
id: 2026-09-18-12h20
date: 2026-09-18
time: "12:20"
title: The proxy already knows who the human is, so the relay can run OAuth without users
---

**Before:** the relay's MCP endpoint carried a bearer token, and the previous note had
settled why — forward-auth is for callers that can follow a redirect, and a program
cannot ([[2026-09-18-11h15-forward-auth-is-for-people-so-a-machine-endpoint-has-to-carry-its-own-credential]]).
The implicit assumption underneath it was that a token is a thing a client can be
*given*.

**What falsified it** is ChatGPT. Its connector dialog has no field for a token at all:
it reads the MCP endpoint's 401, follows the `resource_metadata` header to a discovery
document, finds an authorization server, registers itself, and runs an authorization
code flow. There is no place to paste a secret, so "hand it a credential" is not a thing
that can be done. A server either speaks OAuth 2.1 or that client cannot reach it.

The obvious reading is that this is expensive — an authorization server means accounts,
passwords, sessions, consent records, signing keys, rotation — for a relay whose entire
identity model was "whoever got past Authentik is an operator".

**The delta is that almost none of that is the authorization server's job here, because
the hard part is already solved one layer out.** OAuth's authorization endpoint exists
to establish *which human is present* and get their consent. Authentik establishes the
human on every other path of this host already. So the flow splits along a line that
was there all along:

- the **machine half** — discovery, registration, token exchange — is answered to a
  program, and must be routed past forward-auth for the same reason `/mcp` is;
- the **human half** — one consent page — stays behind forward-auth, and *that is the
  entire user model*. By the time the page renders, someone has proved who they are.

What is left for the relay to implement is bookkeeping: codes, PKCE, redirect matching,
and tokens. And because the relay is both the authorization server and the resource
server, the tokens can be opaque rows in a table it already has, rather than JWTs it
would have to sign and whose keys it would have to rotate. The grants land beside the
tokens the dashboard issues, so one page revokes every kind of credential.

**The general form:** an authorization server is two things bolted together — an
identity provider and a grant ledger. If something upstream is already the identity
provider, what remains is small enough to write. Reading "implement OAuth 2.1" as "build
an identity system" is what made it look out of scope; it was never the part we needed
to build.

One thing this does cost, and it is worth stating plainly: anyone who can get past
Authentik can now approve a connector that commands hardware. That is the same authority
they already had — they could expose a device and drive it from the dashboard — so the
boundary has not moved. But it has become reachable by a different route, and the
consent page says so in words rather than assuming the person knows.

Builds on: [[2026-09-18-11h15-forward-auth-is-for-people-so-a-machine-endpoint-has-to-carry-its-own-credential]].
