# ChordCanvas (VST3)

コードを並べて作曲するための VST3 インストゥルメント。Cubase LE で使えます。

## 使い方
1. Cubase LE で Instrument Track に `ChordCanvas` を読み込む
2. ルート(C〜B)とコードタイプ(maj, m, 7 …)を選び **Add** で並べる
   - **Apply**: 王道進行・Canon などを任意のキーで一括入力
   - 並べたコードをクリックで選択 → ルート/タイプ/長さを変更、`<` `>` で並べ替え
3. Cubase を再生するとプロジェクトのテンポに同期してコード進行がループ再生される
4. **Drag MIDI to track** を Cubase のトラックへドラッグ(または Save .mid)で MIDI として取り込み、好きな音源で鳴らす
- Piano voicing / Guitar voicing(ストラム)切替、Strum ms でかき鳴らし速度を調整

## ビルド (Windows)
### A. GitHub Actions (おすすめ・何もインストール不要)
このフォルダを GitHub にpush → Actions タブの artifact `ChordCanvas-VST3-Windows` をダウンロード。
中の `ChordCanvas.vst3` フォルダを `C:\Program Files\Common Files\VST3\` へコピーして Cubase を再起動(プラグインの再スキャン)。

### B. ローカル
Visual Studio 2022 (C++ デスクトップ開発) と CMake を入れて:
```
cmake -B build
cmake --build build --config Release
```
出力: `build\ChordCanvas_artefacts\Release\VST3\ChordCanvas.vst3`
