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

以下のコマンドでキーマップをコンパイルする。

```bash
# Personal keymap
qmk compile -kb ploopyco/aplus/rev2_001 -km default

# Original keymap
qmk compile -kb ploopyco/aplus/rev2_001 -km org
```

生成されたUF2ファイルは以下に格納される。

```text
.build/ploopyco_aplus_rev2_001_<keymap>.uf2
```

## ブランチ構成

| ブランチ | 用途 |
|---|---|
| `ploopyco/a+` | Ploopy公式ブランチ |
| `develop` | 個人開発用ブランチ |
| `feature/*` | 機能追加・改修 |
| `test/*` | 動作検証 |

## キーコード

| キーコード | 意味 |
|---|---|
| `MS_BTN1` | マウス左クリック |
| `MS_BTN2` | マウス右クリック |
| `MS_BTN3` | マウス中クリック |
| `MS_BTN4` | マウスボタン4 |
| `MS_BTN5` | マウスボタン5 |
| `PKC_DRAG_SCROLL` | ドラッグスクロール（押している間有効） |
| `PKC_GESTURE` | ジェスチャー操作（押している間有効） |
| `TG(LAYER_CONTROL)` | 設定レイヤーの切り替え（旧設定用・現行キーマップでは未使用） |

## カスタマイズ内容

### スクロール

- 左ホイール：縦スクロール
- 右ホイール：横スクロール
- ノブ押下中は対応するスクロールを停止
- Windows / Linuxでは高解像度スクロールを使用
- macOSなどでは低解像度スクロールを使用

### ジェスチャー

`PKC_GESTURE` を押している間、8方向のジェスチャー操作を有効にする。

| 方向 | 操作 | macOS | Windows / Linux |
|---|---|---|---|
| ↑ | Mission Control / Task View | Mission Control | Task View |
| ↗ | Redo | Cmd + Shift + Z | Ctrl + Y |
| → | 次の仮想デスクトップ | Ctrl + → | Ctrl + Win + → / Ctrl + Alt + → |
| ↘ | Paste | Cmd + V | Ctrl + V |
| ↓ | Copy | Cmd + C | Ctrl + C |
| ↙ | Cut | Cmd + X | Ctrl + X |
| ← | 前の仮想デスクトップ | Ctrl + ← | Ctrl + Win + ← / Ctrl + Alt + ← |
| ↖ | Undo | Cmd + Z | Ctrl + Z |

※ Linuxでは上方向のジェスチャーは未割り当て。

### DPI設定

- DPI：600 CPI
- DPI切り替え：なし
- DPIは固定値で運用

### LED

起動時にホストOSを判定し、対応する色を1秒間点灯する。

| OS | LEDカラー |
|---|---|
| macOS | 緑 |
| Windows | 青 |
| その他 | 黄 |

## テスト状況

### ビルド

- [x] 従来版（`default`）のQMKコンパイル
- [ ] 構造体化した最新版のQMKコンパイル
- [ ] 実機への書き込み

### 動作確認

- [ ] 左右ホイール
- [ ] スクロール方向
- [ ] 低速スクロール
- [ ] ドラッグスクロール
- [ ] ジェスチャー
- [ ] OS別ショートカット
- [ ] 起動時LED
- [ ] I2C読み取りエラーからの復帰

## 課題・参考情報

- [Ploopy QMK Firmware - Issues](https://github.com/ploopyco/qmk_firmware/issues)
- [Ploopy QMK Firmware - Pull Requests](https://github.com/ploopyco/qmk_firmware/pulls)