- message subject は 任意の protobuf でデータを送信する。
  - 任意のprotobufの情報はメタデータとして、NATS KVの任意のメタデータ用バケットに、同Subject名をKeyとして、下記 json で保持する。

  ```json
  {
    "schema": {
      "path": "<string: proto file path>", // e.g., google/protobuf/any.proto
      "full_name": "<string: message type name>" // e.g., Any
    }
  }
  ```

- NATS KV の任意のSchema用バケット上にSchema Registry を実現します。
  - .proto の path をKeyとして、任意のFileDescriptorSet を保存する。

- 本リポジトリは上記を実現するための、Utilityライブラリ (C++の場合はヘッダライブラリ)を実現する。
  - 提供するユーティリティは下記
    - Schema登録
    - Schema参照
  - 主なパラメータは以下の通りです。
    - metadata 用バケット名
    - schema 用バケット名
    - Schema Registryへ書き込み側
      - Messageとして使うMessage型
      - 送信先にsubject名
    - Schema Registryを参照する側
      - ターゲットの subject名
      - FileDescriptorSet を取得
