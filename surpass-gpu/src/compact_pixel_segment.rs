#[cfg(not(target_arch = "spirv"))]
use core::convert::TryInto;

use crate::{TILE_HEIGHT, TILE_HEIGHT_SHIFT, TILE_WIDTH, TILE_WIDTH_SHIFT};

#[allow(non_camel_case_types)]
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct c64 {
    #[cfg(target_endian = "little")]
    lo: u32,
    hi: u32,
    #[cfg(target_endian = "big")]
    lo: u32,
}

// impl std::fmt::Debug for CompactPixelSegment {
//     fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
//         f.debug_struct("CompactPixelSegment")
//             .field("is_none", &self.is_none())
//             .field("tile_x", &self.tile_x())
//             .field("tile_y", &self.tile_y())
//             .field("layer_id", &self.layer_id())
//             .field("local_x", &self.local_x())
//             .field("local_y", &self.local_y())
//             .field("area", &self.area())
//             .field("cover", &self.cover())
//             .finish()
//     }
// }

trait Extract<T> {
    fn extract<const SHIFT: u32, const SIZE: u32>(self) -> T;
}

impl Extract<u32> for c64 {
    #[inline]
    fn extract<const SHIFT: u32, const SIZE: u32>(self) -> u32 {
        const fn mask(size: u32) -> u32 {
            let mut mask = 0b1;
            let mut shift = size;

            loop {
                shift -= 1;
                mask |= 0b1 << shift;

                if shift == 0 {
                    break mask;
                }
            }
        }

        match (SHIFT < 32, (SIZE + SHIFT) < 32) {
            (true, true) => (self.lo >> SHIFT) & mask(SIZE),
            (false, false) => (self.hi >> (SHIFT - 32)) & mask(SIZE),
            _ => {
                let lo = (self.lo >> SHIFT) & mask(32 - SHIFT);
                let hi = (self.hi << (32 - SHIFT)) & mask(SIZE);

                lo | hi
            }
        }
    }
}

impl Extract<i32> for c64 {
    #[inline]
    fn extract<const SHIFT: u32, const SIZE: u32>(self) -> i32 {
        let shift = 32 - SIZE as i32;
        let val: u32 = self.extract::<SHIFT, SIZE>();

        (val as i32) << shift >> shift
    }
}

macro_rules! bitfields {
    ( @add [ $size:expr ] ) => ($size);

    ( @add [ $size:expr , $( $sizes_tail:expr ),* ] ) => {{
        $size + bitfields!(@add [$($sizes_tail),*])
    }};

    (
        @shift_or
        $type:ty ,
        $val:ident ,
        $shift:expr ,
        [ $field:ident ] ,
        [ $size:expr $( , )* ]
    ) => {
        $val <<= $shift;
        $val |= $field as $type & (1 << $size) - 1;
    };

    (
        @shift_or
        $type:ty ,
        $val:ident ,
        $shift:expr ,
        [ $field:ident , $( $fields_tail:ident ),* ],
        [ $size:expr , $next_size:expr , $( $sizes_tail:expr ),* $( , )* ]
    ) => {
        $val <<= $shift;
        $val |= $field as $type & (1 << $size) - 1;
        bitfields!(
            @shift_or
            $type,
            $val,
            $next_size,
            [$($fields_tail),*],
            [$next_size, $($sizes_tail),*,]
        );
    };

    ( @getter [ $field:ident ] , [ $type:ty ] , [ $size:expr ] ) => {
        #[inline]
        pub fn $field(self) -> $type {
            self.0.extract::<0, { $size }>()
        }
    };

    (
        @getter
        [ $field:ident , $( $fields_tail:ident ),* ] ,
        [ $type:ty , $( $types_tail:ty ),* ] ,
        [ $size:expr , $( $sizes_tail:expr ),* ]
    ) => {
        #[inline]
        pub fn $field(self) -> $type {
            self.0.extract::<{ bitfields!(@add [$($sizes_tail),*]) }, { $size }>()
        }

        bitfields!(@getter [$($fields_tail),*], [$($types_tail),*], [$($sizes_tail),*]);
    };

    (
        @impl
        $type:ty ,
        [ $( $fields:ident ),* ] ,
        [ $( $types:ty ),* ] ,
        [ $( $sizes:expr ),* ]
    ) => {
        #[cfg(not(target_arch = "spirv"))]
        #[inline]
        pub fn new(
            $($fields: $types),*
        ) -> Self {
            let mut val = 0u64;

            bitfields!(@shift_or u64, val, 0, [$($fields),*], [$($sizes),*]);

            val.into()
        }

        bitfields!(@getter [$($fields),*], [$($types),*], [$($sizes),*]);
    };

    (
        $type:ty {
            $( $fields:ident : $field_types:ty [ $sizes:expr ] ),*
            $( , )?
        }
    ) => {
        bitfields!(@impl $type, [$($fields),*], [$($field_types),*], [$($sizes),*]);
    };
}

#[repr(transparent)]
#[derive(Clone, Copy, Default, Debug)]
pub struct CompactPixelSegment(c64);

impl CompactPixelSegment {
    bitfields! {
        c64 {
            is_none: u32[1],
            tile_y: i32[15 - TILE_HEIGHT_SHIFT],
            tile_x: i32[16 - TILE_WIDTH_SHIFT],
            layer_id: u32[16],
            local_y: u32[TILE_HEIGHT_SHIFT],
            local_x: u32[TILE_WIDTH_SHIFT],
            area: i32[10],
            cover: i32[6],
        }
    }

    pub fn new_xy(x: i32, y: i32, layer_id: u32, area: i32, cover: i32) -> Self {
        CompactPixelSegment::new(
            0,
            y >> TILE_HEIGHT_SHIFT,
            (x >> TILE_WIDTH_SHIFT).max(-1) + 1,
            layer_id,
            y as u32 & (TILE_HEIGHT - 1) as u32,
            x as u32 & (TILE_WIDTH - 1) as u32,
            area,
            cover,
        )
    }
}

#[cfg(not(target_arch = "spirv"))]
impl From<u64> for CompactPixelSegment {
    fn from(val: u64) -> Self {
        let bytes = val.to_ne_bytes();

        Self(c64 {
            lo: u32::from_ne_bytes(bytes[0..4].try_into().unwrap_or_default()),
            hi: u32::from_ne_bytes(bytes[4..8].try_into().unwrap_or_default()),
        })
    }
}
