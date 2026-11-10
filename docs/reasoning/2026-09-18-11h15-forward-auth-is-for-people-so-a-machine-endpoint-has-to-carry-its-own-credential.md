---
id: 2026-09-18-11h15
date: 2026-09-18
time: "11:15"
title: Forward-auth is for people, so a machine endpoint has to carry its own credential
---

**Before:** the relay had exactly two kinds of path, and the split had held since it was
deployed. Everything a person touches sits behind the reverse proxy's forward-auth
(Authentik decides, the relay never asks who is calling); `/device` is excluded, because
a device cannot follow a login redirect, and instead proves itself with a token the
relay checks. Two kinds of caller, two mechanisms, and no third case.

**What the MCP surface exposed** is that those two categories were never "human vs
device" — they were "can follow a redirect" vs "cannot". `/mcp` is browser-reachable in
the sense that it is HTTP on the human side of the proxy, and it is used by a program,
which cannot follow a 302 to an OAuth screen any more than an ESP32 can. Deploying it
behind forward-auth produced an endpoint that was live, healthy, correct — and answered
every MCP client with a redirect to a login page. Nothing was broken; the classification
was.

**The delta:** the proxy authenticates *sessions*, and a session is a thing a browser
has. Anything without one has to carry its credential on every request, and the check
belongs in the process that owns what is being protected rather than in a label on a
container — because that process is the only place that knows what a refusal should say,
and the only place a missing secret can be made to mean "refuse everything" instead of
"allow everything".

That last part is the half worth writing down. The dangerous default is not a weak
token, it is an *absent* one: a proxy rule that routes a path past authentication, plus a
relay that treats an unset token as "no check configured", publishes a device-driving API
to the internet and looks completely healthy from outside. So an unconfigured token
refuses every request and says so at startup, and the two halves of the change — the
route that removes the proxy's check and the code that adds its own — went in together,
because either alone is a hole.

What did NOT change, and deliberately: no users, scopes, refresh tokens or OAuth. One
machine credential answers "is this the machine we gave it to". Which devices that
machine may then reach is a different question with a different answer already in place —
the per-device MCP switch, which is the authorization boundary. Two simple gates in
series, each answering one question, beat one elaborate gate answering both badly.

Builds on: [[2026-09-18-10h50-a-device-that-describes-itself-needs-no-tool-of-its-own]].
