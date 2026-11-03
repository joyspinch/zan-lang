# Zan.Web3

Pure-Zan Web3 & Blockchain foundation library, built without native C dependencies.

## Modules

- `Zan.Web3.Cryptography`:
  - `Keccak256`: Ethereum-standard Keccak-256 sponge hash (FIPS 202 pre-NIST 0x01 padding).
  - `Secp256k1`: Koblitz curve $y^2 = x^3 + 7 \pmod p$, keypair derivation, and point arithmetic.
- `Zan.Web3.Types`:
  - `Address`: 20-byte Ethereum address representation with EIP-55 checksum parsing and formatting.
  - `EtherUnit`: Conversions between Wei, Gwei, and Ether.
- `Zan.Web3.Encoding`:
  - `Rlp`: Recursive Length Prefix serialization and deserialization for transactions and blocks.
  - `Abi`: Ethereum contract Application Binary Interface encoding/decoding and function selector calculation.
- `Zan.Web3.Rpc`:
  - Ethereum JSON-RPC payload builders and response models.
