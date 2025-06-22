---
title: キーマップのカスタマイズ
description: DYA Dash のキーマップカスタマイズ方法の解説
sidebar:
  order: 2
---

DYA Dash のキーマップを変更する方法を解説したページです。
自由度と難易度の違う３つの方法があります。

- **ZMK Studio**: いちばん簡単にカスタマイズできる方法です。プログラミングなし、Github レポジトリなしでできますが、複雑なカスタマイズはできません。
- **keymap editor**: Github レポジトリを作る必要がありますが、プログラミングなしで ZMK Studio よりも複雑なカスタマイズができます。
- **プログラミング**: プログラミングの知識が必要ですが自由自在にカスタマイズができます。

## ZMK Studio を使ったカスタマイズ

ファームウェアを書き換えずに簡単なキーマッピングの変更ができます。

以下の２種類の方法があります。

- ブラウザ上で動く公式サイト https://zmk.studio/ (USB 接続時のみ対応)
- PC アプリ https://zmk.studio/download (無線接続時も対応)

ブラウザ上で動くサイトの使い方を解説します。

1. 右手キーボードの XIAO の USB ポートを PC と接続
2. 右手の LED が 3 つ以上緑に点灯することを確認。(USB 優先モード)
   - 光らない場合は BLE 優先モードになっているので、左手の設定ボタン左を押してください。
   - BLE 接続がされている場合、対応する LED だけは青く光ります
3. https://zmk.studio/ を開く
4. サイトで USB を選択して出てくるポップアップで DYA Dash KB を選択
5. キーボードの左手左下キー+その右隣のキーで、Stduio unlock コマンドを実行
   - (初期ファームウェアでは常に unlock 状態でしたが、セキュリティ上の懸念があるらしいので変更しました)

あとはいい感じにがんばって編集して保存してください。

## keymap editor を使ったカスタマイズ

ファームウェアのソースコードを自動で書き換えてくれるノーコードツール https://nickcoutsos.github.io/keymap-editor/ を使ったカスタマイズ方法です。
ZMK Studio ではできないマクロなどの高度なカスタマイズができます。

### 設定レポジトリを Github に準備する

1. https://github.com/ のアカウントを作ります。
2. https://github.com/cormoran/zmk-keyboard-dya-dash をテンプレートとして設定レポジトリを作ります。

   ![](./img/keymap_editor1.png)

3. 自動的にファームウェアのビルドが始まることを確認します。

   - 作成されたレポジトリの "Actions" タブをクリックすると "Initial commit" という workflow が動いていると思います。
   - ビルドが終わるとその workflow の Summary ページの Artifacts に zip 圧縮されたファームウェア一式をダウンロードできるリンクが出現します。

   ![](./img/keymap_editor2.png)
   ![](./img/keymap_editor3.png)
   ![](./img/keymap_editor4.png)

### keymap editor で設定を編集

1.  https://nickcoutsos.github.io/keymap-editor/ を開いて、github アカウントと連携します。

    | GitHub を選択                 | Login with GitHub             |
    | ----------------------------- | ----------------------------- |
    | ![](./img/keymap_editor5.png) | ![](./img/keymap_editor6.png) |

    | Add Repository                | All repository か、上で作成したレポジトリを個別に選ぶ |
    | ----------------------------- | ----------------------------------------------------- |
    | ![](./img/keymap_editor7.png) | ![](./img/keymap_editor8.png)                         |

2.  keymap editor で 2 で作ったレポジトリを選択して各種設置をカスタマイズして Save します

    - カスタマイズの方法などはネットで調べてください。https://sensai-gadget.com/keymap-editor-usage/ などは情報が多そうです。

    ![](./img/keymap_editor9.png)

3.  github のレポジトリに戻ると新しい変更が増えていてファームウェアのビルドが始まっています
4.  ビルドが完了したら Actions の Summary からファームウェアの zip をダウンロードして、キーボードにファームウェアを書き込みます。

### 右側の設定ボタンのカスタマイズ

右手の設定ボタンは Combo 機能でカスタマイズします。
下の写真のような矢印キーの組み合わせで Combo を登録すると動きます。
レイヤーごとに別の機能を設定するのが少し面倒ですがご了承ください。

なお、矢印キーを同時押しした時もこの Combo が動いてしまいます。矢印キー同時押しをしたい場合は、設定ボタンが使えなくなりますが Combo を消すことで対応できます。

![](./img/keymap_editor_right_setting.png)

## プログラミングによるカスタマイズ

keymap editor を使ったカスタマイズの "設定レポジトリを Github に準備する" に従ってレポジトリ作ります。
あとはレポジトリの readme などを見ながらがんばってください。

main ブランチに push するとビルドが始まるほか、ビルドツールをインストールすればローカルでビルドもできるようになっています。
