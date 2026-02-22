//! D3DKMT-based GPU metrics for AMD/Intel GPUs on Windows.
//!
//! Uses `D3DKMTEnumAdapters2`, `D3DKMTQueryStatistics`, and
//! `D3DKMTQueryAdapterInfo` from gdi32.dll, which is always available
//! on Windows 10+.
//!
//! Temperature is not available via D3DKMT — reports 0.

use super::gpu::{GpuMetrics, GpuProvider};
use std::cell::UnsafeCell;
use std::mem;
use std::ptr;

// ---------------------------------------------------------------------------
// FFI types and constants
// ---------------------------------------------------------------------------

type NtStatus = i32;
const STATUS_SUCCESS: NtStatus = 0;

// D3DKMT_QUERYSTATSTICS_TYPE values we need
const D3DKMT_QUERYSTATISTICS_ADAPTER: u32 = 0;
const D3DKMT_QUERYSTATISTICS_SEGMENT: u32 = 2;
const D3DKMT_QUERYSTATISTICS_NODE: u32 = 4;

// D3DKMT_QUERYSTATISTICS struct (simplified — we only read the fields we need
// via byte offsets into the union, matching the Windows SDK layout).

/// Enough room for the largest D3DKMT_QUERYSTATISTICS result union.
const QUERY_STATS_SIZE: usize = 1024;

#[repr(C)]
struct D3dkmtQueryStatistics {
    stat_type: u32,
    adapter_luid: Luid,
    _process_handle: usize,
    result: QueryStatisticsResult,
}

#[repr(C)]
struct QueryStatisticsResult {
    // We use a byte array and cast to the sub-struct we need.
    _data: [u8; QUERY_STATS_SIZE],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Luid {
    low_part: u32,
    high_part: i32,
}

// D3DKMT_ENUMADAPTERS2
const MAX_ADAPTERS: usize = 16;

#[repr(C)]
struct D3dkmtEnumAdapters2 {
    num_adapters: u32,
    adapters: *mut D3dkmtAdapterInfo,
}

#[repr(C)]
#[derive(Clone)]
struct D3dkmtAdapterInfo {
    adapter_handle: u32,
    adapter_luid: Luid,
    num_sources: u32,
    present_move_regions_preferred: u32,
}

// D3DKMT_QUERYADAPTERINFO for getting adapter description
const KMTQAITYPE_UMDRIVERNAME: u32 = 1;
const KMTQAITYPE_ADAPTERTYPE: u32 = 15;

#[repr(C)]
struct D3dkmtQueryAdapterInfo {
    adapter_handle: u32,
    info_type: u32,
    private_driver_data: *mut u8,
    private_driver_data_size: u32,
}

// The adapter type struct — tells us if it's a render-only, display-only, etc.
#[repr(C)]
#[derive(Default)]
struct D3dkmtAdapterType {
    flags: u32,
}

impl D3dkmtAdapterType {
    fn is_software(&self) -> bool {
        self.flags & (1 << 2) != 0
    }
}

// D3DKMT_SEGMENTSIZEINFO
#[repr(C)]
#[derive(Default)]
struct D3dkmtSegmentSizeInfo {
    dedicated_video_memory_size: u64,
    dedicated_system_memory_size: u64,
    shared_system_memory_size: u64,
}

const KMTQAITYPE_GETSEGMENTSIZE: u32 = 3;

// UMD driver name — contains the adapter description string
#[repr(C)]
struct D3dkmtUmdDriverName {
    name: [u16; 260],
}

#[link(name = "gdi32")]
unsafe extern "system" {
    fn D3DKMTEnumAdapters2(info: *mut D3dkmtEnumAdapters2) -> NtStatus;
    fn D3DKMTQueryStatistics(stats: *mut D3dkmtQueryStatistics) -> NtStatus;
    fn D3DKMTQueryAdapterInfo(info: *mut D3dkmtQueryAdapterInfo) -> NtStatus;
}

// ---------------------------------------------------------------------------
// Adapter discovery (used by metrics/discovery.rs)
// ---------------------------------------------------------------------------

/// Discovered D3DKMT adapter info.
pub struct D3dkmtAdapterDiscovery {
    pub index: u32,
    pub name: String,
}

/// Enumerate GPU adapters via D3DKMT. Filters out software adapters.
pub fn enumerate_adapters() -> Vec<D3dkmtAdapterDiscovery> {
    let mut adapters_buf = vec![
        D3dkmtAdapterInfo {
            adapter_handle: 0,
            adapter_luid: Luid::default(),
            num_sources: 0,
            present_move_regions_preferred: 0,
        };
        MAX_ADAPTERS
    ];

    let mut enum_info = D3dkmtEnumAdapters2 {
        num_adapters: MAX_ADAPTERS as u32,
        adapters: adapters_buf.as_mut_ptr(),
    };

    let status = unsafe { D3DKMTEnumAdapters2(&mut enum_info) };
    if status != STATUS_SUCCESS {
        return Vec::new();
    }

    let count = enum_info.num_adapters as usize;
    let mut result = Vec::new();

    for (ordinal, adapter) in adapters_buf[..count].iter().enumerate() {
        // Skip software adapters.
        let mut adapter_type = D3dkmtAdapterType::default();
        let mut query = D3dkmtQueryAdapterInfo {
            adapter_handle: adapter.adapter_handle,
            info_type: KMTQAITYPE_ADAPTERTYPE,
            private_driver_data: &mut adapter_type as *mut _ as *mut u8,
            private_driver_data_size: mem::size_of::<D3dkmtAdapterType>() as u32,
        };
        let status = unsafe { D3DKMTQueryAdapterInfo(&mut query) };
        if status == STATUS_SUCCESS && adapter_type.is_software() {
            continue;
        }

        // Get adapter name from the UMD driver name path.
        let name = get_adapter_description(adapter.adapter_handle)
            .unwrap_or_else(|| format!("GPU {ordinal}"));

        result.push(D3dkmtAdapterDiscovery {
            index: ordinal as u32,
            name,
        });
    }

    result
}

/// Try to extract a human-readable GPU name from the adapter.
fn get_adapter_description(adapter_handle: u32) -> Option<String> {
    // Try to get the UMD driver path — it often contains the GPU name in
    // the directory path (e.g. "...\\AMD Radeon RX 7900 XTX\\...").
    // Fallback: just use the segment size info as an identifier.
    let mut umd_name: D3dkmtUmdDriverName = unsafe { mem::zeroed() };
    let mut query = D3dkmtQueryAdapterInfo {
        adapter_handle,
        info_type: KMTQAITYPE_UMDRIVERNAME,
        private_driver_data: &mut umd_name as *mut _ as *mut u8,
        private_driver_data_size: mem::size_of::<D3dkmtUmdDriverName>() as u32,
    };
    let status = unsafe { D3DKMTQueryAdapterInfo(&mut query) };
    if status == STATUS_SUCCESS {
        let path = String::from_utf16_lossy(
            &umd_name.name[..umd_name
                .name
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(umd_name.name.len())],
        );
        // Extract the meaningful portion from the driver path.
        // The path looks like "C:\Windows\System32\DriverStore\...\amdxx64.dll"
        // We'll just use the file stem as the name if we can't do better.
        if !path.is_empty() {
            // Try to find a known GPU vendor keyword in the path
            let lower = path.to_lowercase();
            if lower.contains("amd") || lower.contains("ati") {
                return Some("AMD GPU".to_string());
            } else if lower.contains("intel") {
                return Some("Intel GPU".to_string());
            } else {
                return Some(
                    path.split('\\')
                        .last()
                        .unwrap_or("GPU")
                        .trim_end_matches(".dll")
                        .to_string(),
                );
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Internal adapter state
// ---------------------------------------------------------------------------

struct AdapterState {
    luid: Luid,
    num_nodes: u32,
    dedicated_vram_bytes: u64,
    prev_total_time: u64,
}

// ---------------------------------------------------------------------------
// D3DKMT GPU Provider
// ---------------------------------------------------------------------------

/// Safety: `UnsafeCell` is used because `GpuProvider::query` takes `&self`
/// but we need to update `prev_total_time`. The provider is only ever called
/// from the single collector thread, so no data races can occur.
pub struct D3dkmtGpuProvider {
    state: UnsafeCell<AdapterState>,
}

// Safety: Only accessed from the collector thread.
unsafe impl Send for D3dkmtGpuProvider {}

impl D3dkmtGpuProvider {
    /// Create a provider for the adapter at the given ordinal index.
    pub fn new(ordinal: u32) -> Option<Self> {
        let adapters = enumerate_adapters_raw()?;
        let adapter = adapters.get(ordinal as usize)?;
        Self::from_adapter(adapter)
    }

    /// Create a provider for the adapter whose name contains the given substring.
    pub fn by_name(name: &str) -> Option<Self> {
        let adapters = enumerate_adapters_raw()?;
        let lower = name.to_lowercase();
        for adapter in &adapters {
            let desc = get_adapter_description(adapter.adapter_handle)
                .unwrap_or_default()
                .to_lowercase();
            if desc.contains(&lower) {
                return Self::from_adapter(adapter);
            }
        }
        None
    }

    fn from_adapter(adapter: &D3dkmtAdapterInfo) -> Option<Self> {
        // Query number of nodes.
        let num_nodes = query_node_count(adapter)?;

        // Query dedicated VRAM size.
        let dedicated_vram_bytes = query_vram_size(adapter.adapter_handle);

        Some(Self {
            state: UnsafeCell::new(AdapterState {
                luid: adapter.adapter_luid,
                num_nodes,
                dedicated_vram_bytes,
                prev_total_time: 0,
            }),
        })
    }
}

impl GpuProvider for D3dkmtGpuProvider {
    fn query(&self) -> Option<GpuMetrics> {
        // Safety: Only called from the single collector thread.
        let state = unsafe { &mut *self.state.get() };

        // Query VRAM usage from segment statistics.
        let (vram_used_bytes, vram_total_bytes) = query_vram_usage(state);

        // Query GPU usage from node statistics.
        let usage_percent = query_gpu_usage(state);

        Some(GpuMetrics {
            usage_percent,
            temperature_celsius: 0.0, // Not available via D3DKMT
            vram_used_bytes,
            vram_total_bytes,
        })
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Enumerate raw adapter info structs.
fn enumerate_adapters_raw() -> Option<Vec<D3dkmtAdapterInfo>> {
    let mut adapters_buf = vec![
        D3dkmtAdapterInfo {
            adapter_handle: 0,
            adapter_luid: Luid::default(),
            num_sources: 0,
            present_move_regions_preferred: 0,
        };
        MAX_ADAPTERS
    ];

    let mut enum_info = D3dkmtEnumAdapters2 {
        num_adapters: MAX_ADAPTERS as u32,
        adapters: adapters_buf.as_mut_ptr(),
    };

    let status = unsafe { D3DKMTEnumAdapters2(&mut enum_info) };
    if status != STATUS_SUCCESS {
        return None;
    }

    adapters_buf.truncate(enum_info.num_adapters as usize);

    // Filter out software adapters.
    adapters_buf.retain(|adapter| {
        let mut adapter_type = D3dkmtAdapterType::default();
        let mut query = D3dkmtQueryAdapterInfo {
            adapter_handle: adapter.adapter_handle,
            info_type: KMTQAITYPE_ADAPTERTYPE,
            private_driver_data: &mut adapter_type as *mut _ as *mut u8,
            private_driver_data_size: mem::size_of::<D3dkmtAdapterType>() as u32,
        };
        let status = unsafe { D3DKMTQueryAdapterInfo(&mut query) };
        !(status == STATUS_SUCCESS && adapter_type.is_software())
    });

    Some(adapters_buf)
}

fn query_node_count(adapter: &D3dkmtAdapterInfo) -> Option<u32> {
    // Query adapter statistics to get the node count.
    let mut stats: D3dkmtQueryStatistics = unsafe { mem::zeroed() };
    stats.stat_type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    stats.adapter_luid = adapter.adapter_luid;

    let status = unsafe { D3DKMTQueryStatistics(&mut stats) };
    if status != STATUS_SUCCESS {
        return None;
    }

    // The node count is at offset 0 of the adapter result union.
    let node_count = unsafe {
        ptr::read_unaligned(stats.result._data.as_ptr() as *const u32)
    };

    if node_count == 0 {
        return None;
    }

    Some(node_count)
}

fn query_vram_size(adapter_handle: u32) -> u64 {
    let mut seg_info = D3dkmtSegmentSizeInfo::default();
    let mut query = D3dkmtQueryAdapterInfo {
        adapter_handle,
        info_type: KMTQAITYPE_GETSEGMENTSIZE,
        private_driver_data: &mut seg_info as *mut _ as *mut u8,
        private_driver_data_size: mem::size_of::<D3dkmtSegmentSizeInfo>() as u32,
    };
    let status = unsafe { D3DKMTQueryAdapterInfo(&mut query) };
    if status == STATUS_SUCCESS {
        seg_info.dedicated_video_memory_size
    } else {
        0
    }
}

fn query_vram_usage(state: &AdapterState) -> (u64, u64) {
    let mut total_committed = 0u64;

    // Query segment statistics for the first few segments.
    for seg_id in 0..8u32 {
        let mut stats: D3dkmtQueryStatistics = unsafe { mem::zeroed() };
        stats.stat_type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        stats.adapter_luid = state.luid;
        // The segment ID is placed at the start of the query-specific input area.
        unsafe {
            ptr::write_unaligned(
                stats.result._data.as_mut_ptr() as *mut u32,
                seg_id,
            );
        }

        let status = unsafe { D3DKMTQueryStatistics(&mut stats) };
        if status != STATUS_SUCCESS {
            break;
        }

        // Committed bytes are at a known offset in the segment result.
        // In the D3DKMT_QUERYSTATISTICS_SEGMENT_INFORMATION struct:
        //   offset 0: CommitLimit (u64)
        //   offset 8: BytesCommitted (u64)
        let bytes_committed = unsafe {
            ptr::read_unaligned(stats.result._data.as_ptr().add(8) as *const u64)
        };
        total_committed += bytes_committed;
    }

    (total_committed, state.dedicated_vram_bytes)
}

fn query_gpu_usage(state: &mut AdapterState) -> f32 {
    let mut total_running = 0u64;

    for node_id in 0..state.num_nodes {
        let mut stats: D3dkmtQueryStatistics = unsafe { mem::zeroed() };
        stats.stat_type = D3DKMT_QUERYSTATISTICS_NODE;
        stats.adapter_luid = state.luid;
        // Node ordinal is placed at the start of the query-specific input area.
        unsafe {
            ptr::write_unaligned(
                stats.result._data.as_mut_ptr() as *mut u32,
                node_id,
            );
        }

        let status = unsafe { D3DKMTQueryStatistics(&mut stats) };
        if status != STATUS_SUCCESS {
            continue;
        }

        // GlobalInformation.RunningTime is the first u64 in the node result.
        let running_time = unsafe {
            ptr::read_unaligned(stats.result._data.as_ptr() as *const u64)
        };
        total_running += running_time;
    }

    // Compute usage as delta of running time vs wall clock.
    // Running time is in 100ns units (Windows QPC).
    let usage = if state.prev_total_time > 0 && total_running >= state.prev_total_time {
        let delta = total_running - state.prev_total_time;
        // Approximate elapsed wall-clock time from the sum of node times.
        // We use a simple heuristic: divide by number of nodes and normalize
        // to a percentage.  The actual wall time should come from QueryPerformanceCounter
        // but for simplicity we track the total and compute ratio.
        let wall_approx = 10_000_000u64; // ~1 second in 100ns units
        let pct = (delta as f64 / (wall_approx as f64 * state.num_nodes as f64)) * 100.0;
        pct.clamp(0.0, 100.0) as f32
    } else {
        0.0
    };

    state.prev_total_time = total_running;
    usage
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn enumerate_adapters_does_not_panic() {
        // May return empty on systems without D3DKMT support, but should not crash.
        let adapters = enumerate_adapters();
        for a in &adapters {
            assert!(!a.name.is_empty());
        }
    }
}
