use std::{
    fs, io,
    process::{Command, Output},
    str,
};

fn print_output(result: io::Result<Output>) {
    if let Ok(output) = result {
        if !output.status.success() {
            panic!("{}", str::from_utf8(&output.stdout).unwrap());
        }
    }
}

fn main() {
    let out_dir = fs::canonicalize("../out").unwrap();

    fs::create_dir_all(&out_dir).unwrap();

    print_output(
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
            .output(),
    );
    print_output(Command::new("ninja").current_dir(&out_dir).output());

    println!("cargo:rerun-if-changed=../cassia");
}
