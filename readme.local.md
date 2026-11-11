# Ploopy A+

## 関連リンク

- [A+ マニュアル](https://ploopyco.github.io/aplus-trackball/)
- [A+ ハードウェアリポジトリ](https://github.com/ploopyco/aplus-trackball)
- [Ploopy QMK Firmware](https://github.com/ploopyco/qmk_firmware)

## ファームウェア

- [A+ ファームウェア](./keyboards/ploopyco/aplus/)
- [Rev2.001](./keyboards/ploopyco/aplus/rev2_001/)
- [キーマップ](./keyboards/ploopyco/aplus/keymaps/)

## コンパイル

以下のコマンドでデフォルトのキーマップをコンパイルする。

```bash
qmk compile -kb ploopyco/aplus/rev2_001 -km default

// original
qmk compile -kb ploopyco/aplus/rev2_001 -km org
```

生成されたUF2ファイルは以下に格納される。

```text
.build/ploopyco_aplus_rev2_001_default.uf2
```

## ブランチ構成

| ブランチ      | 用途               |
| ------------- | ------------------ |
| `ploopyco/a+` | Ploopy公式ブランチ |
| `develop`     | 個人開発用ブランチ |
| `feature/*`   | 機能追加・改修     |
| `test/*`      | 動作検証           |

## 課題・参考情報

- [Ploopy QMK Firmware - Issues](https://github.com/ploopyco/qmk_firmware/issues)
- [Ploopy QMK Firmware - Pull Requests](https://github.com/ploopyco/qmk_firmware/pulls)
