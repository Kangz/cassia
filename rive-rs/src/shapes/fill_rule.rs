use crate::core::TryFromU64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FillRule {
    NonZero,
    EvenOdd,
}

impl Default for FillRule {
    fn default() -> Self {
        Self::NonZero
    }
}

impl TryFromU64 for FillRule {
    fn try_from(value: u64) -> Option<Self> {
        match value {
            0 => Some(Self::NonZero),
            1 => Some(Self::EvenOdd),
            _ => None,
        }
    }
}
