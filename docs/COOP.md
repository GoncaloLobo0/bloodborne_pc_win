# Co-op (work in progress)

Goal: summoning and co-op between two PCs running this port. Bloodborne's online play needs
PSN and FromSoftware's servers, which a PC port cannot use, so the port has to provide both
sides itself. Nothing here works for players yet.

## What the game uses (from its imports and strings)

- **FromSoftware game server over HTTP(S)** (`sceHttp`, form-encoded requests, user agent
  `PS4Application FROMhttp/1.0 (E-EP9000-CUSA03173_00-BLOODBORNE0000EU/2014101500)`,
  `Content-Type: text/txt`). Endpoints, from the game's own names: `api_Login`,
  `api_ServerTimeGet`, `api_SyncCharaId`, `api_NoticeNormalGet`, `api_NoticeEmergencyGet`,
  `api_UserAgreementGet`, `api_BloodMess{Create,GetList,Evaluate,GetEvaluate,Remove,SearchAdd}`
  (messages), `api_TombMess{Create,GetList,Remove}` and `api_DeathVisionGet` (bloodstains),
  `api_Channel*` (chalice dungeon glyphs), `api_SummonData{Create,GetList,Remove,Summon}`
  (summon signs / bells), `api_WanderingGhost{Create,Get}`, `api_ChairMess*`,
  `api_MessengerShellUpload`, `api_MultiPlayNetError`, `api_UserPropertiesMoveCount*`.
  The server's address is not in plain text in the executable (built or decoded at run time).
- **PSN matchmaking and peer to peer**: `sceNpMatching2` (rooms, 27 functions),
  `sceNpSignaling`, `sceNet` sockets / epoll, `sceNpAuthGetAuthorizationCode` (login token),
  `sceVoice`.

## Plan

1. **Observe** (started): `BB_ONLINE=1` makes the runtime report a connected network and a
   PSN sign-in, and logs every network call (`Online: ...` lines). Nothing is sent anywhere.
2. **Server**: a small HTTP server implementing the `api_*` endpoints; the runtime sends the
   game's requests to it (never to Sony or FromSoftware).
3. **Matchmaking**: `sceNpMatching2` rooms and `sceNpSignaling` through that server; `sceNet`
   sockets mapped to real sockets for the peer-to-peer traffic.
4. **Play**: summoning between two PCs, then whatever the game's sync needs.

## Where it stands

With `BB_ONLINE=1` the game initializes HTTP (template above), NetCtl and NP state callbacks
(`sceNetCtlRegisterCallback`, `sceNpRegisterStateCallback`), `sceNpMatching2Initialize`, then
waits (`sceHttpWaitRequest`, `sceNetEpollWait`). It never calls `sceNpCheckCallback`,
`sceNetCtlCheckCallback` or `sceNpGetState` in the title screen and the first area, so the
sign-in sequence has not started yet. Next steps:

- find what starts the game's network sequence (FrpgNetMan steps: `FrpgNetSysStep`,
  `FrpgNetConnectStep`, `FrpgNetLobbyStep`; the in-game *Network Settings*; the state
  callbacks, which the runtime never invokes);
- invoke the NP / NetCtl state callbacks (signed in, IP obtained) like the system would;
- capture the first `api_Login` request (URL, body) and decode its format.
