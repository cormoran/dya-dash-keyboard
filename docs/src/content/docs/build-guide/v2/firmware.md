---
title: ファームウェアの書き込み
description: XIAO nRF52840へのファームウェアインストール
sidebar:
  order: 2
---

はんだ付けを始める前に、2 つの XIAO nRF52840 にファームウェアを書き込みます。

## 必要なもの

- XIAO nRF52840 × 2 個
- USB ケーブル（Type-C、データ通信対応）
- PC

## ファームウェアファイルのダウンロード

1. [GitHub リリースページ](https://github.com/cormoran/dya-dash-keyboard/releases)を開きます
2. 最新版の以下のファイルをダウンロードします
   - 右トラックボールの場合
     - 右手: `zmk_dya_dash_right_trackball.uf2`
     - 左手: `zmk_dya_dash_left.uf2`
   - 左トラックボールの場合
     - 右手: `zmk_dya_dash_right.uf2`
     - 左手: `zmk_dya_dash_left_trackball.uf2`
   - 両方トラックボールの場合
     - 右手: `zmk_dya_dash_right_trackball.uf2`
     - 左手: `zmk_dya_dash_left_trackball.uf2`
   - トラックボールなしの場合
     - 右手: `zmk_dya_dash_right.uf2`
     - 左手: `zmk_dya_dash_left.uf2`

## 書き込み手順

### 1 つ目の XIAO（左手用）

1. **XIAO を PC に接続**

   - 1 つ目の XIAO nRF52840 を USB ケーブルで PC と接続します

2. **書き込みモードに入る**

   - XIAO のリセットボタンを素早く 2 回押します
   - 詳しくはインターネットを調べ手ください。例えば[このサイト](https://qiita.com/KentaHarada/items/3e1612116ec45462a837#2-%E5%A4%89%E6%8F%9B%E3%81%97%E3%81%9Fuf2%E3%83%95%E3%82%A1%E3%82%A4%E3%83%AB%E3%82%92xiao-nrf52840%E3%81%AB%E6%9B%B8%E3%81%8D%E8%BE%BC%E3%82%80)など

3. **ファームウェアをコピー**

   - PC に「XIAO-SENSE」という名前の USB メモリのようなデバイスが表示されます
   - 上で指定の左用のファームウェアをドラッグ&ドロップでコピーします

4. **書き込み完了**
   - 「XIAO-SENSE」が自動で切断されます

### 2 つ目の XIAO（右手用）

同僚の手順で右手用のファイルを書き込みます。

どちらの XIAO に左右どちらのファームウェアを書き込んだか覚えておいてください

### 左右がわからなくなった場合

もし左右がわからなくなった場合でも、XIAO をはんだ付けした後に再度正しいファームウェアを書き込むことができます。

左右を逆に取り付けても回路は壊れませんが、キーが逆転したりトラックボールが動かなかったりします。

## トラブルシューティング

### XIAO が認識されない場合

- USB ケーブルがデータ通信対応かご確認ください
- 異なる USB ポートを試してみてください
- XIAO のリセットボタンをもっと素早く 2 回押してください
