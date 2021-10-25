use std::path::PathBuf;

use structopt::StructOpt;

mod cassia;
mod circles;
mod rive;

#[derive(StructOpt)]
#[structopt(about = "Cassia demo with multiple modes")]
enum Demo {
    /// Draws random circles
    Circles {
        /// Amount of circles to draw
        #[structopt(default_value = "100")]
        count: usize,
    },
    /// Renders a Rive animation
    Rive {
        /// .riv input file
        #[structopt(parse(from_os_str))]
        file: PathBuf,
    },
}

fn main() {
    match Demo::from_args() {
        Demo::Circles { count } => circles::circles_main(count),
        Demo::Rive { file } => rive::rive_main(file),
    }
}
