## 0.3.0

* Support Flutter 3.44 / Dart 3: raise Dart SDK constraint to `>=3.0.0 <4.0.0`.
* Migrate analyzer options from the removed `strong-mode: implicit-casts` to `language: strict-casts`.
* Bump `flutter_lints` to `^5.0.0`.
* Raise macOS minimum deployment target to 10.15.
* Add Swift Package Manager support for macOS (CocoaPods still supported).

## 0.2.0
* Added the ability to determine whether a created window will be resizable or not
([#101](https://github.com/MixinNetwork/flutter-plugins/issues/101) and [#130](https://github.com/MixinNetwork/flutter-plugins/pull/130))

## 0.1.0

* [BREAK CHANGE] upgrade min flutter version to 3.0.0
* fix macOS memory leak issue. [#123](https://github.com/MixinNetwork/flutter-plugins/issues/123)

## 0.0.2

* [Windows] fix free window_channel_ may cause crash. [#78](https://github.com/MixinNetwork/flutter-plugins/pull/78)
* add getAllSubWindowIds api. [#77](https://github.com/MixinNetwork/flutter-plugins/pull/77)

## 0.0.1

* Initial release. support Linux, macOS, Windows.
