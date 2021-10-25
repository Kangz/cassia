use crate::core::{Core, ObjectRef, OnAdded};

#[derive(Debug, Default)]
pub struct Backboard;

impl Core for Backboard {}

impl OnAdded for ObjectRef<'_, Backboard> {
    on_added!();
}
