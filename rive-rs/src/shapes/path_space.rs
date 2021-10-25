use flagset::{flags, FlagSet};

flags! {
    pub enum PathSpace: u8 {
        Local,
        World,
        Difference,
        Clipping,
    }
}
