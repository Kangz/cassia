use crate::core::{Core, ObjectRef};

pub(crate) struct Animator<T> {
    val: T,
}

impl<T: Clone> Animator<T> {
    pub fn new(val: T) -> Self {
        Self { val }
    }

    pub fn animate<'a, C: Core, S>(&self, object: &'a ObjectRef<'a, C>, setter: S)
    where
        S: Fn(&'a ObjectRef<'a, C>, T),
    {
        setter(object, self.val.clone());
    }
}
