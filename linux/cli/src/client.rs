use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::path::Path;
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use serde_json::Value;

const MAX_FRAME_BYTES: u32 = 16 * 1024 * 1024;

pub struct Client {
    stream: UnixStream,
}

impl Client {
    pub fn connect<P: AsRef<Path>>(path: P, timeout: Duration) -> Result<Self> {
        let stream = UnixStream::connect(path.as_ref()).with_context(|| {
            format!(
                "connect {}: is rawaccel-agentd running?",
                path.as_ref().display()
            )
        })?;
        stream.set_read_timeout(Some(timeout))?;
        stream.set_write_timeout(Some(timeout))?;
        Ok(Self { stream })
    }

    pub fn call(&mut self, request: &Value) -> Result<Value> {
        let payload = serde_json::to_vec(request)?;
        let len = u32::try_from(payload.len())
            .map_err(|_| anyhow!("request too large"))?;
        if len > MAX_FRAME_BYTES {
            return Err(anyhow!("request exceeds MAX_FRAME_BYTES"));
        }
        self.stream.write_all(&len.to_be_bytes())?;
        self.stream.write_all(&payload)?;

        let mut len_buf = [0u8; 4];
        self.stream.read_exact(&mut len_buf)?;
        let resp_len = u32::from_be_bytes(len_buf);
        if resp_len > MAX_FRAME_BYTES {
            return Err(anyhow!("response exceeds MAX_FRAME_BYTES"));
        }
        let mut buf = vec![0u8; resp_len as usize];
        self.stream.read_exact(&mut buf)?;

        serde_json::from_slice(&buf).context("agent returned invalid JSON")
    }
}
