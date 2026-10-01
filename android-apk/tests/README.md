# Host regression checks

`HostRegressionTest.java` runs with JDK 17, the SDK's `android.jar` and the app
classes produced by `:app:assembleRelease` (or `:app:assembleDebug`). It exercises Java-only methods; Android
stub methods are never called. Arguments are a HMM schema file, its `mod.ini`,
and an existing directory for temporary fixtures. Input files are read only.

```powershell
$jdk = "$env:JAVA_HOME/bin"
$android = "$env:LOCALAPPDATA/Android/Sdk/platforms/android-34/android.jar"
$classes = "android-apk/app/build/intermediates/javac/release/compileReleaseJavaWithJavac/classes"
$tests = "out/issue-tests"
New-Item -ItemType Directory -Force $tests | Out-Null
& "$jdk/javac.exe" -cp "$android;$classes" -d $tests android-apk/tests/HostRegressionTest.java
& "$jdk/java.exe" -cp "$tests;$android;$classes" org.libsdl.app.HostRegressionTest schema.json mod.ini $tests
```

The supplied regression schema writes `Main.IncludeDir0` to `mod.ini` and has
trailing commas in arrays and objects. The runner checks quoted strings,
comments, a BOM, INI preservation and reload, verified storage copying, path
rebasing, populated destinations, cancellation, DLC ZIP extraction and traversal
rejection. It leaves normalized JSON fixtures for validation with a strict JSON
parser. No mod/game assets are included in this repository.

`tests/directory_transaction_test.cpp` can be compiled with a C++20 compiler and
run with one argument: a new directory for fixtures. It checks DLC directory
replacement, restoration after a failed publication, and both interruption
windows between publication and backup cleanup.

Device checks are still required: import real DLC containers and DLC-only ZIPs
over an installed game, repeat the import, configure the mod through Android's
spinner, and copy an installation to a mounted SD card. Verify launch, saves and
enabled mods after restarting the app, then test an unavailable selected card.

Run `python android-apk/tests/check_translations.py` from the repository root to
check locale coverage, duplicate keys, array sizes, format placeholders and
resource keys referenced by localized Java exceptions and native dialogs.
