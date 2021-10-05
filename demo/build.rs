use std::{fs, process::Command};

fn main() {
    let out_dir = fs::canonicalize("../out").unwrap();

    fs::create_dir_all(&out_dir).unwrap();

    Command::new("cmake")
        .current_dir(&out_dir)
        .arg("../cassia")
        .arg("-G")
        .arg("Ninja")
        .arg("-D")
        .arg("DAWN_ENABLE_OPENGLES=0")
        .arg("-D")
        .arg("DAWN_ENABLE_DESKTOP_GL=0")
        .arg("-D")
        .arg("CMAKE_POSITION_INDEPENDENT_CODE=1")
        .status()
        .unwrap();
    Command::new("ninja")
        .current_dir(&out_dir)
        .status()
        .unwrap();

    println!("cargo:rerun-if-changed=../dawn");
}
