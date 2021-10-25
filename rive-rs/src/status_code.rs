#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StatusCode {
    Ok,
    MissingObject,
    InvalidObject,
    FailedInversion,
}
