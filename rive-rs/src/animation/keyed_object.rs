use crate::{
    animation::KeyedProperty,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    status_code::StatusCode,
    Artboard,
};

#[derive(Debug, Default)]
pub struct KeyedObject {
    object_id: Property<u64>,
    keyed_properties: DynVec<Object<KeyedProperty>>,
}

impl ObjectRef<'_, KeyedObject> {
    pub fn object_id(&self) -> u64 {
        self.object_id.get()
    }

    pub fn set_object_id(&self, object_id: u64) {
        self.object_id.set(object_id)
    }
}

impl ObjectRef<'_, KeyedObject> {
    pub fn push_keyed_property(&self, keyed_property: Object<KeyedProperty>) {
        self.keyed_properties.push(keyed_property);
    }

    pub fn apply(&self, artboard: Object<Artboard>, time: f32, mix: f32) {
        if let Some(core) = artboard.as_ref().resolve(self.object_id() as usize) {
            for property in self.keyed_properties.iter() {
                property.as_ref().apply(core.clone(), time, mix);
            }
        }
    }
}

impl Core for KeyedObject {
    properties![(51, object_id, set_object_id)];
}

impl OnAdded for ObjectRef<'_, KeyedObject> {
    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        if context.resolve(self.object_id() as usize).is_none() {
            return StatusCode::MissingObject;
        }

        for property in self.keyed_properties.iter() {
            property.as_ref().on_added_dirty(context);
        }

        StatusCode::Ok
    }

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        for property in self.keyed_properties.iter() {
            property.as_ref().on_added_clean(context);
        }

        StatusCode::Ok
    }
}
