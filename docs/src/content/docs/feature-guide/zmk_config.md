---
title: ZMK Config を変更する
description: ZMK Config の変更方法
sidebar:
  order: 6
---

キーマップなどの設定ファイル https://github.com/cormoran/zmk-keyboard-dya-dash の解説ページです。
トラックボールを触った時に自動的にマウスレイヤーに切り替わる設定や、トラックボールの感度などの調整方法を解説しています。

## 編集すべきファイル

- [config/dya_dash.keymap](https://github.com/cormoran/zmk-keyboard-dya-dash/blob/main/config/dya_dash.keymap)(v2), [config/dya_dash_v3.keymap](https://github.com/cormoran/zmk-keyboard-dya-dash/blob/main/config/dya_dash_v3.keymap) (v3)に設定を調整したいと思われる項目が集まっています。基本的にはこのファイルを編集すれば十分です
- [boards/shields/dya_dash](https://github.com/cormoran/zmk-keyboard-dya-dash/tree/main/boards/shields/dya_dash)(v2), [boards/shields/dya_dash_v3](https://github.com/cormoran/zmk-keyboard-dya-dash/tree/main/boards/shields/dya_dash_v3)(v3) ディレクトリ以下にさまざまな設定ファイル/定義ファイルが入っています。より高度な設定をする場合はこちらの編集が必要かもしれません

## 変更例

:::note

V2 の古いファームウェア向けの内容です。DYA Studio 対応したバージョンからは設定が大きく変わっており、以下で解説している内容はすべて DYA Studio でファームウェア書き換えなしに設定が変更できます。

:::

### トラックボール関連の設定を変える

[config/dya_dash.keymap](https://github.com/cormoran/zmk-keyboard-dya-dash/blob/main/config/dya_dash.keymap) の以下の部分がトラックボール関連の設定です。
ZMK の [Input Processor](https://zmk.dev/docs/keymaps/input-processors) という機能の設定が書かれています。

```
# 右側トラックボールの設定
&trackball_listener {
    input-processors = <
    // TRACKBALL_L layer is activated when trackball is used.
    // After 500ms of trackball inactivity, the layer is deactivated.
    &zip_temp_layer TRACKBALL_L 500
    // Example: Adjust sensitivity of trackball multiply by 1 and divide by 60.
    // &zip_xy_scaler 1 60
    >;
};

&trackball_scroller {
    layers = <SCROLL_L>;
    // Example: adjust sensitivity of scroll multiply by 1 and divide by 60.
    // input-processors = <&zip_xy_scaler 1 60 &zip_xy_to_scroll_mapper>;
};
# 左側トラックボールの設定
&trackball_listener_l {input-processors = <&zip_temp_layer TRACKBALL_L 500>; };

&trackball_scroller_l {layers = <SCROLL_L>; };

&zip_temp_layer {excluded-positions = <19 20 21 22 32 33 41 52 53 54 55 56 57 58 59>; };

&trackball_r {cpi = <600>; };
&trackball_l {cpi = <600>; };
```

#### オートマウスレイヤーを無効にする

`&zip_temp_layer TRACKBALL_L 500` の部分を削除することでトラックボールを触った時に自動的にマウスレイヤーに切り替わる設定を無効にできます。

```diff
&trackball_listener {
   input-processors = <
--    // TRACKBALL_L layer is activated when trackball is used.
--    // After 500ms of trackball inactivity, the layer is deactivated.
--    &zip_temp_layer TRACKBALL_L 500
   // Example: Adjust sensitivity of trackball multiply by 1 and divide by 60.
   // &zip_xy_scaler 1 60
   >;
```

代わりに、 `keymap` でマウスレイヤーに移動するキーマップ (`&mo TRACKBALL_L` など)を追加する必要があるかもしれません。
デフォルトのキーマップでは外側のタッチセンサーに触れている間マウスレイヤーが有効になるようになっています。

#### トラックボールの感度を調整する

センサーの CPI を設定するか、ZMK の input processor で変化の倍率を調整する２つの方法があります

センサーの CPI は以下のような修正で変更できます
CPI を高くすることで少しの操作でより移動量を増やすことができます。
CPI は 200 刻みで 200~3200 の間で設定できます。

```diff
-- &trackball_r {cpi = <600>; };
++ &trackball_r {cpi = <800>; };
&trackball_l {cpi = <600>; };
```

ZMK の input processor を使う場合は以下のように `zip_xy_scaler` を設定します。
以下の例ではセンサーの入力を xy に 1/60 にして y に 2/60 にしています。

```diff
&trackball_listener {
    input-processors = <
    // TRACKBALL_L layer is activated when trackball is used.
    // After 500ms of trackball inactivity, the layer is deactivated.
    &zip_temp_layer TRACKBALL_L 500
++    // Example: Adjust sensitivity of trackball multiply by 1 and divide by 60.
++    &zip_xy_scaler 1 60
++    // Change sensitivity for y direction * 2 / 60 (=/30)
++    &zip_y_scaler 2 60
    >;
};
```

#### トラックボールのスクロールの感度を調整する

スクロール時の動作は `trackball_scroller` という部分で input processor の設定を上書きできるようになっています。
例えば以下の設定ではスクロール時には入力を 1/60 にします。

```diff
&trackball_scroller {
    layers = <SCROLL_L>;
++    // Example: adjust sensitivity of scroll multiply by 1 and divide by 60.
++  input-processors = <
++    &zip_xy_scaler 1 60
++    &zip_xy_to_scroll_mapper
++  >;
};
```

### スリープに入るまでの時間を調整する

[dya_dash_left.conf](https://github.com/cormoran/zmk-keyboard-dya-dash/blob/main/boards/shields/dya_dash/dya_dash_left.conf), [dya_dash_right.conf](https://github.com/cormoran/zmk-keyboard-dya-dash/blob/main/boards/shields/dya_dash/dya_dash_right.conf) に書かれている以下の項目を変更するとスリープに入るまでの時間を調整できます。

```diff
# Deep sleep (BLE オフにして電力消費を最小限にする)
-- # 5min
-- CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=300000
++ # 30min
++ CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=1800000
# idle (BLE は接続を継続するが、LED など電力を多く消費するデバイスをオフにする)
-- #3s
-- CONFIG_ZMK_IDLE_TIMEOUT=30000
++ #1min
++ CONFIG_ZMK_IDLE_TIMEOUT=60000
```

左側（Peripheral) は消費電力が少ないので deep sleep に入るまでの時間を延ばしておくと使いやすいと思います。

## その他

質問が来たら随時追加していきますのでお気軽に X の DM や Booth のメッセージからお問い合わせください。
