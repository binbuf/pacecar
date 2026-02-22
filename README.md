# 🏎️ Pacecar

**Pacecar** is a lightweight, always-on-top system performance overlay built with **Rust** and **egui**. It provides real-time CPU, RAM, GPU, network, and disk I/O metrics in a compact, modern dashboard—similar to Windows Task Manager's Performance tab but as a floating overlay.

Designed for minimal overhead and a polished aesthetic, Pacecar aims to keep you informed about your system's health without getting in your way.

## ✨ Features

- **Real-time Metrics**: Monitor CPU usage (total/per-core), RAM, GPU (NVIDIA support), Network speeds, and Disk I/O.
- **Modern UI**: Clean, compact grid layout using circular gauges or sparklines.
- **Always-on-Top**: Keep the dashboard visible while gaming or working.
- **Overlay Modes**:
  - **Interactive**: Drag to reposition and access settings.
  - **Click-through**: Mouse events pass through to windows below for zero interference.
- **System Tray Integration**: Minimize to tray, toggle visibility, and switch modes from the taskbar.
- **Global Hotkey**: Toggle the overlay instantly with `Ctrl+Shift+P` (default).
- **Low Footprint**: Optimized for <15MB memory usage.

## 🚀 Getting Started

### Prerequisites

- **Rust**: Ensure you have the [Rust toolchain](https://rustup.rs/) installed (Edition 2024).
- **Windows 11/10**: Currently optimized for Windows (MVP). Cross-platform support for macOS/Linux is planned.

### Installation & Running

1. **Clone the repository**:
   ```bash
   git clone https://github.com/user/pacecar.git
   cd pacecar
   ```

2. **Run in development mode**:
   ```bash
   cargo run
   ```

3. **Build and run for production**:
   For the best performance and lowest memory footprint, use the release profile:
   ```bash
   cargo run --release
   ```

### Features (Optional)

To enable NVIDIA GPU monitoring (requires NVML):
```bash
cargo run --release --features nvidia
```

## 🛠️ Development

### Project Structure
- `src/main.rs`: Entry point and app lifecycle.
- `src/metrics/`: Collection logic for CPU, RAM, GPU, etc.
- `src/ui/`: egui widget and layout implementation.
- `src/overlay.rs`: Window transparency and click-through logic.

### Running Tests
Pacecar uses a test-driven approach for core logic:
```bash
cargo test
```

## Credits

- App icon: [Races Speed](https://www.svgrepo.com/svg/273959/races-speed) from SVG Repo (CC0 License)

## 📄 License
This project is licensed under the MIT License - see the LICENSE file for details.
