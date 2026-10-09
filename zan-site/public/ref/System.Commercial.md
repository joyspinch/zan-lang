# System.Commercial

> 源码: `packages/Zan.Commercial/src/System/Commercial/LicenseClient.zan`


## LicenseCheckResult (class)

- public bool ok;

- public string reason;

- public int graceSeconds;

- public long expiresAt;

- public string message;

- static LicenseCheckResult Fail(string why, string msg)


## LicenseClient (class)

- string serverUrl;

- string vendor;

- string product;

- LicenseState state;

- string vendorKeyHex="";

- string vendorKeyExpHex="010001";

- static LicenseClient shared;

- static LicenseClient Shared()

- void Server(string url, string vendorName)

- void Product(string productId)

- void VendorPublicKey(string modulusHex, string exponentHex)

- static string CertCanonical(string product, string deviceFp, string subject, long expiresAt)

- static string MakeCertificate(string product, string deviceFp, string subject, long expiresAt, string modulusHex, string privateExponentHex)

- string StateAuthKey()

- static string StateCanonical(LicenseState s)

- string StateAuth(LicenseState s)

- bool StateAuthOk()

- bool SignedCertOk()

- LicenseCheckResult InstallCertificate(string certJson)

- string DeviceFp()

- public string Fingerprint()

- public string StateFile()

- string StateDir()

- string StatePath()

- void EnsureLoaded()

- void Persist()

- void Forget()

- async LicenseCheckResult Activate(string code)

- async LicenseCheckResult Login(string account, string password)

- async LicenseCheckResult Check()

- async LicenseCheckResult Heartbeat()

- void Logout()

- LicenseCheckResult OkResult(long now)

- async LicenseCheckResult Heartbeat(bool quiet)

- async LicenseCheckResult PostActivate(string code, string unused)

- async LicenseCheckResult PostLogin(string account, string password)

- async string PostJson(string path, string json)

- async LicenseCheckResult LogoutOnline()


## LicenseState (class)

- public LicenseState()

- public string mode;

- public string product;

- public string subject;

- public string deviceFp;

- public string sessionToken;

- public string salt;

- public string mac;

- public string certSig;

- public long expiresAt;

- public int graceSeconds;

- public long lastHeartbeat;
