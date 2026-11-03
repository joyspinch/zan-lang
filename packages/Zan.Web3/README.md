# Zan.Web3

Pure-Zan Web3 & Ethereum application protocol suite, built on top of `Zan.Security` without native C dependencies.

## Modules

- `Zan.Web3.Types`:
  - `Address`: 20-byte Ethereum address representation with EIP-55 checksum parsing, formatting, and `ecrecover` address recovery.
  - `Account`: High-level wallet account facade supporting private key generation, EIP-191 personal message signing/verification, and offline transaction signing.
  - `Transaction`: Modern EIP-1559 (Type-2) dynamic fee transaction model and offline signing pipeline.
  - `EtherUnit`: Conversions between Wei, Gwei, and Ether.
- `Zan.Web3.Encoding`:
  - `Rlp`: Recursive Length Prefix serialization and deserialization for transactions and blocks.
  - `Abi`: Ethereum contract Application Binary Interface encoding/decoding and function selector calculation.
- `Zan.Web3.Rpc`:
  - `JsonRpcBuilder`: Ethereum JSON-RPC 2.0 payload builders for standard node interactions (Geth, Reth, Erigon, Infura, Alchemy).

## Cryptography Dependency

Underlying cryptographic primitives (`Keccak256`, `Secp256k1`, `Secp256k1Signature`) reside in the `System.Security.Cryptography` namespace within the `Zan.Security` package.

