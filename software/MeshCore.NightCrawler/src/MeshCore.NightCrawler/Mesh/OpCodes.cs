namespace MeshCore.NightCrawler.Mesh;

/// <summary>
/// The single place MeshCore companion-protocol opcodes live. Values verified
/// against meshcore-dev/MeshCore firmware v1.17.1 (examples/companion_radio +
/// examples/simple_repeater) and the meshcore_py reference client.
///
/// If a firmware bump moves an opcode, this is the only file that changes.
/// </summary>
public static class OpCodes
{
    // ---- App -> device commands (CMD_*) ----
    public const byte CmdAppStart        = 1;
    public const byte CmdGetContacts     = 4;
    public const byte CmdAddUpdateContact= 9;    // 0x09
    public const byte CmdGetDeviceTime   = 5;
    public const byte CmdSetDeviceTime   = 6;
    public const byte CmdResetPath       = 13;   // 0x0D
    public const byte CmdDeviceQuery     = 22;   // 0x16
    public const byte CmdSendLogin       = 26;   // 0x1A
    public const byte CmdLogout          = 29;   // 0x1D
    public const byte CmdGetContactByKey = 30;   // 0x1E
    public const byte CmdGetAdvertPath   = 42;   // 0x2A
    public const byte CmdSendBinaryReq   = 50;   // 0x32
    public const byte CmdPathDiscovery   = 52;   // 0x34
    public const byte CmdGetStats        = 56;   // 0x38
    public const byte CmdSendAnonReq     = 57;   // 0x39
    public const byte CmdSetPathHashMode = 61;   // 0x3D  (payload: 0x00, mode)

    // ---- Device -> app immediate responses (RESP_CODE_*) ----
    public const byte RespOk            = 0;
    public const byte RespError         = 1;
    public const byte RespContactsStart = 2;
    public const byte RespContact       = 3;
    public const byte RespContactsEnd   = 4;
    public const byte RespSelfInfo      = 5;
    public const byte RespSent          = 6;   // carries the 4-byte tag
    public const byte RespCurrTime      = 9;
    public const byte RespDeviceInfo    = 13;
    public const byte RespAdvertPath    = 22;

    // ---- Device -> app async pushes (PUSH_CODE_*, high bit set) ----
    public const byte PushAdvert            = 0x80;
    public const byte PushPathUpdated       = 0x81;
    public const byte PushLoginSuccess      = 0x85;
    public const byte PushLoginFailed       = 0x86;
    public const byte PushStatusResponse    = 0x87;
    public const byte PushTelemetryResponse = 0x8B;
    public const byte PushBinaryResponse    = 0x8C;   // reply to anon + binary requests
    public const byte PushPathDiscoveryResp = 0x8D;
    public const byte PushNewAdvert         = 0x8A;

    // ---- Repeater request types carried inside CMD_SEND_BINARY_REQ ----
    public const byte ReqTypeGetStatus     = 0x01;
    public const byte ReqTypeGetTelemetry  = 0x03;
    public const byte ReqTypeGetAccessList = 0x05;
    public const byte ReqTypeGetNeighbours = 0x06;
    public const byte ReqTypeGetOwnerInfo  = 0x07;

    // ---- Anonymous request sub-types carried inside CMD_SEND_ANON_REQ ----
    public const byte AnonReqRegions = 0x01;
    public const byte AnonReqOwner   = 0x02;
    public const byte AnonReqBasic   = 0x03;

    // ---- Advert / contact types ----
    public const byte AdvTypeNone     = 0;
    public const byte AdvTypeChat     = 1;
    public const byte AdvTypeRepeater = 2;
    public const byte AdvTypeRoom     = 3;
    public const byte AdvTypeSensor   = 4;

    // ---- ACL permission tiers ----
    public const byte PermRoleMask = 0x03;
    public const byte PermGuest    = 0x00;
    public const byte PermReadOnly = 0x01;
    public const byte PermReadWrite= 0x02;
    public const byte PermAdmin    = 0x03;

    public static string RoleName(byte advType) => advType switch
    {
        AdvTypeChat => "companion",
        AdvTypeRepeater => "repeater",
        AdvTypeRoom => "roomserver",
        AdvTypeSensor => "sensor",
        _ => "unknown",
    };
}
