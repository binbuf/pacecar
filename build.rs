fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default() == "windows" {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("assets/app.ico");
        if std::env::var("CARGO_FEATURE_UAC").is_ok() {
            res.set_manifest_file("assets/app.manifest");
            println!("cargo:rerun-if-changed=assets/app.manifest");
        }
        res.compile().expect("failed to compile Windows resources");
    }

    // Copy install-pawnio.ps1 next to the output binary so it's available at runtime.
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default() == "windows" {
        let manifest_dir_str = std::env::var("CARGO_MANIFEST_DIR").unwrap();
        let script_src = std::path::Path::new(&manifest_dir_str).join("install-pawnio.ps1");
        if script_src.exists() {
            println!("cargo:rerun-if-changed=install-pawnio.ps1");
            let out_dir = std::env::var("OUT_DIR").unwrap();
            let out = std::path::Path::new(&out_dir);
            if let Some(target_dir) = out.ancestors().nth(3) {
                let _ = std::fs::copy(&script_src, target_dir.join("install-pawnio.ps1"));
            }
        }
    }

    // Build hwmon-shim NativeAOT DLL if the shim directory exists and we're targeting Windows.
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default() == "windows" {
        let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
        let shim_dir = std::path::Path::new(&manifest_dir).join("hwmon-shim");

        if shim_dir.exists() {
            println!("cargo:rerun-if-changed=hwmon-shim/HwMon.cs");
            println!("cargo:rerun-if-changed=hwmon-shim/hwmon-shim.csproj");

            let out_dir = std::env::var("OUT_DIR").unwrap();
            let publish_dir = std::path::Path::new(&out_dir).join("hwmon-shim-publish");

            let status = std::process::Command::new("dotnet")
                .args([
                    "publish",
                    "-c",
                    "Release",
                    "-r",
                    "win-x64",
                    "-o",
                ])
                .arg(publish_dir.to_str().unwrap())
                .current_dir(&shim_dir)
                .status();

            match status {
                Ok(s) if s.success() => {
                    println!(
                        "cargo:rustc-link-search=native={}",
                        publish_dir.to_str().unwrap()
                    );

                    // Copy the DLL next to the output binary so it's found at runtime.
                    // Walk up from OUT_DIR to find the profile directory (e.g. target/debug/).
                    let out = std::path::Path::new(&out_dir);
                    if let Some(target_dir) = out.ancestors().nth(3) {
                        let src = publish_dir.join("hwmon-shim.dll");
                        let dst = target_dir.join("hwmon-shim.dll");
                        if src.exists() {
                            let _ = std::fs::copy(&src, &dst);
                        }
                    }

                    eprintln!("[build.rs] hwmon-shim built successfully");
                }
                Ok(s) => {
                    println!("cargo:warning=dotnet publish failed with status {s} — hwmon DLL will not be available");
                }
                Err(e) => {
                    println!("cargo:warning=dotnet not found ({e}) — hwmon DLL will not be available");
                }
            }
        }
    }
}
