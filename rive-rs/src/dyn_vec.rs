use std::{cell::RefCell, cmp::Ordering};

#[derive(Debug)]
pub struct DynVec<T> {
    vec: RefCell<Vec<T>>,
}

impl<T> DynVec<T> {
    pub fn new() -> Self {
        Self {
            vec: RefCell::new(Vec::new()),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.vec.borrow().is_empty()
    }

    pub fn len(&self) -> usize {
        self.vec.borrow().len()
    }

    pub fn push(&self, val: T) {
        self.vec.borrow_mut().push(val);
    }

    pub fn sort_by<F>(&self, compare: F)
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        self.vec.borrow_mut().sort_by(compare);
    }
}

impl<T: Clone> DynVec<T> {
    pub fn iter(&self) -> DynVecIter<T> {
        DynVecIter {
            vec: &self.vec,
            index: 0,
        }
    }

    pub fn index(&self, index: usize) -> T {
        self.vec.borrow()[index].clone()
    }
}

impl<T> Default for DynVec<T> {
    fn default() -> Self {
        Self::new()
    }
}

pub struct DynVecIter<'v, T> {
    vec: &'v RefCell<Vec<T>>,
    index: usize,
}

impl<T: Clone> Iterator for DynVecIter<'_, T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        let val = self.vec.borrow().get(self.index).cloned();
        self.index += 1;
        val
    }
}
