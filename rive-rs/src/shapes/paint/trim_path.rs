use crate::{
    component::Component,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct TrimPath {
    component: Component,
    start: Property<f32>,
    end: Property<f32>,
    offset: Property<f32>,
    mode_value: Property<u64>,
}

// todo!()

impl ObjectRef<'_, TrimPath> {
    pub fn start(&self) -> f32 {
        self.start.get()
    }

    pub fn set_start(&self, start: f32) {
        self.start.set(start);
    }

    pub fn end(&self) -> f32 {
        self.end.get()
    }

    pub fn set_end(&self, end: f32) {
        self.end.set(end);
    }

    pub fn offset(&self) -> f32 {
        self.offset.get()
    }

    pub fn set_offset(&self, offset: f32) {
        self.offset.set(offset);
    }

    pub fn mode_value(&self) -> u64 {
        self.mode_value.get()
    }

    pub fn set_mode_value(&self, mode_value: u64) {
        self.mode_value.set(mode_value);
    }
}

impl Core for TrimPath {
    parent_types![(component, Component)];

    properties![
        (114, start, set_start),
        (115, end, set_end),
        (116, offset, set_offset),
        (117, mode_value, set_mode_value),
        component,
    ];
}

impl OnAdded for ObjectRef<'_, TrimPath> {
    on_added!(Component);
}
