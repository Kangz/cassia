use std::{cell::Cell, fmt};

pub struct OptionCell<T>(Cell<Option<T>>);

impl<T> OptionCell<T> {
    pub fn new() -> Self {
        Self(Cell::new(None))
    }
}

impl<T: Clone> OptionCell<T> {
    pub fn get(&self) -> Option<T> {
        let option = self.0.take();
        self.0.set(option.clone());
        option
    }
}

impl<T> OptionCell<T> {
    pub fn set(&self, val: Option<T>) {
        self.0.set(val);
    }

    pub fn with<U>(&self, mut f: impl FnMut(Option<&T>) -> U) -> U {
        let val = self.0.take();
        let result = f(val.as_ref());
        self.0.set(val);

        result
    }
}

impl<T> Default for OptionCell<T> {
    fn default() -> Self {
        Self(Cell::new(None))
    }
}

impl<T: fmt::Debug> fmt::Debug for OptionCell<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.with(|val| fmt::Debug::fmt(&val, f))
    }
}
