# ![Logo](chrome/app/theme/chromium/product_logo_64.png) Chromium

out/Default/args.gn
```
# WORKING Chrome build - DISABLE ALL ML SERVICES
target_cpu = "arm64"
host_cpu = "arm64"
is_component_build = true
is_debug = false
symbol_level = 0

# NUCLEAR DISABLE ALL ML CRAP
use_xnnpack = false
enable_tflite = false
enable_ml_internal = false
enable_webnn = false
enable_on_device_model = false
enable_ml_service = false

# Keep essential
enable_supervised_users = true
ffmpeg_branding = "Chrome"
proprietary_codecs = true
```

Chromium is an open-source browser project that aims to build a safer, faster,
and more stable way for all users to experience the web.

The project's web site is https://www.chromium.org.

To check out the source code locally, don't use `git clone`! Instead,
follow [the instructions on how to get the code](docs/get_the_code.md).

Documentation in the source is rooted in [docs/README.md](docs/README.md).

Learn how to [Get Around the Chromium Source Code Directory
Structure](https://www.chromium.org/developers/how-tos/getting-around-the-chrome-source-code).

For historical reasons, there are some small top level directories. Now the
guidance is that new top level directories are for product (e.g. Chrome,
Android WebView, Ash). Even if these products have multiple executables, the
code should be in subdirectories of the product.

If you found a bug, please file it at https://crbug.com/new.
