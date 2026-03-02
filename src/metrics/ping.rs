/// Ping latency metrics via ICMP.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct PingMetrics {
    /// Round-trip latency in milliseconds, or `None` if the ping failed/timed out.
    pub latency_ms: Option<f32>,
}

/// Collect a single ICMP ping to `target` (an IPv4 address string like `"8.8.8.8"`).
///
/// On Windows this uses the `IcmpSendEcho` API which requires no admin privileges.
/// On other platforms this returns `None`.
pub fn collect_ping(target: &str) -> PingMetrics {
    PingMetrics {
        latency_ms: platform_ping(target),
    }
}

#[cfg(target_os = "windows")]
fn platform_ping(target: &str) -> Option<f32> {
    use std::mem;
    use std::net::Ipv4Addr;
    use std::ptr;

    // IP address types
    type IpAddr = u32;

    #[repr(C)]
    struct IpOptionInformation {
        ttl: u8,
        tos: u8,
        flags: u8,
        options_size: u8,
        options_data: *mut u8,
    }

    #[repr(C)]
    struct IcmpEchoReply {
        address: IpAddr,
        status: u32,
        round_trip_time: u32,
        data_size: u16,
        reserved: u16,
        data: *mut u8,
        options: IpOptionInformation,
    }

    #[link(name = "iphlpapi")]
    unsafe extern "system" {
        fn IcmpCreateFile() -> isize;
        fn IcmpCloseHandle(handle: isize) -> i32;
        fn IcmpSendEcho(
            icmp_handle: isize,
            destination: IpAddr,
            request_data: *const u8,
            request_size: u16,
            request_options: *const IpOptionInformation,
            reply_buffer: *mut u8,
            reply_size: u32,
            timeout: u32,
        ) -> u32;
    }

    const INVALID_HANDLE: isize = -1;
    const TIMEOUT_MS: u32 = 1000;

    // Parse the target as an IPv4 address.
    let addr: Ipv4Addr = target.parse().ok()?;
    let ip_addr = u32::from_ne_bytes(addr.octets());

    unsafe {
        let handle = IcmpCreateFile();
        if handle == INVALID_HANDLE || handle == 0 {
            return None;
        }

        let send_data = b"pacecar\0";
        let reply_size = mem::size_of::<IcmpEchoReply>() + send_data.len() + 8;
        let mut reply_buf = vec![0u8; reply_size];

        let num_replies = IcmpSendEcho(
            handle,
            ip_addr,
            send_data.as_ptr(),
            send_data.len() as u16,
            ptr::null(),
            reply_buf.as_mut_ptr(),
            reply_size as u32,
            TIMEOUT_MS,
        );

        let result = if num_replies > 0 {
            let reply = &*(reply_buf.as_ptr() as *const IcmpEchoReply);
            if reply.status == 0 {
                // IP_SUCCESS
                Some(reply.round_trip_time as f32)
            } else {
                None
            }
        } else {
            None
        };

        IcmpCloseHandle(handle);
        result
    }
}

#[cfg(not(target_os = "windows"))]
fn platform_ping(_target: &str) -> Option<f32> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_ping_metrics() {
        let m = PingMetrics::default();
        assert_eq!(m.latency_ms, None);
    }

    #[test]
    fn collect_ping_invalid_address_returns_none() {
        let m = collect_ping("not_an_ip");
        assert_eq!(m.latency_ms, None);
    }
}
