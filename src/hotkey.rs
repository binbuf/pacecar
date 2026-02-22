// Global hotkey registration and handling

use global_hotkey::hotkey::{Code, HotKey, Modifiers};
use global_hotkey::{GlobalHotKeyEvent, GlobalHotKeyManager};

pub const DEFAULT_HOTKEY: &str = "Ctrl+Shift+P";

/// Parsed hotkey action from polling.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum HotkeyAction {
    ToggleOverlay,
}

/// Manages global hotkey registration and event polling.
pub struct HotkeyManager {
    _manager: GlobalHotKeyManager,
    hotkey: HotKey,
}

impl HotkeyManager {
    /// Create a new HotkeyManager, registering the hotkey from the given string.
    /// Falls back to the default hotkey if parsing fails.
    /// Returns `None` if registration fails entirely (e.g., hotkey already taken).
    pub fn new(hotkey_str: &str) -> Option<Self> {
        let hotkey = match parse_hotkey(hotkey_str) {
            Ok(hk) => hk,
            Err(e) => {
                eprintln!(
                    "warn: invalid hotkey '{hotkey_str}': {e}, falling back to default"
                );
                match parse_hotkey(DEFAULT_HOTKEY) {
                    Ok(hk) => hk,
                    Err(e) => {
                        eprintln!("error: failed to parse default hotkey: {e}");
                        return None;
                    }
                }
            }
        };

        let manager = match GlobalHotKeyManager::new() {
            Ok(m) => m,
            Err(e) => {
                eprintln!("warn: failed to create hotkey manager: {e}");
                return None;
            }
        };

        if let Err(e) = manager.register(hotkey) {
            eprintln!(
                "warn: failed to register hotkey: {e}, continuing without global hotkey"
            );
            return None;
        }

        Some(Self {
            _manager: manager,
            hotkey,
        })
    }

    /// Poll for hotkey events. Returns `Some(HotkeyAction)` if the registered hotkey was pressed.
    pub fn poll(&self) -> Option<HotkeyAction> {
        if let Ok(event) = GlobalHotKeyEvent::receiver().try_recv() {
            if event.id() == self.hotkey.id() {
                return Some(HotkeyAction::ToggleOverlay);
            }
        }
        None
    }

    /// Returns the registered hotkey's id.
    pub fn hotkey_id(&self) -> u32 {
        self.hotkey.id()
    }
}

/// Parse a user-friendly hotkey string like "Ctrl+Shift+P" into a `HotKey`.
///
/// Supported modifier names: Ctrl/Control, Shift, Alt, Super/Win/Meta
/// Supported key names: A-Z, 0-9, F1-F12, Space, Enter, Tab, Escape, etc.
pub fn parse_hotkey(s: &str) -> Result<HotKey, String> {
    let parts: Vec<&str> = s.split('+').map(str::trim).collect();
    if parts.is_empty() {
        return Err("empty hotkey string".to_string());
    }

    let mut modifiers = Modifiers::empty();
    let mut key_code = None;

    for part in &parts {
        match part.to_lowercase().as_str() {
            "ctrl" | "control" => modifiers |= Modifiers::CONTROL,
            "shift" => modifiers |= Modifiers::SHIFT,
            "alt" => modifiers |= Modifiers::ALT,
            "super" | "win" | "meta" => modifiers |= Modifiers::SUPER,
            other => {
                if key_code.is_some() {
                    return Err(format!("multiple non-modifier keys found: '{other}'"));
                }
                key_code = Some(parse_key_code(other)?);
            }
        }
    }

    let code = key_code.ok_or_else(|| "no key specified (only modifiers)".to_string())?;
    let mods = if modifiers.is_empty() {
        None
    } else {
        Some(modifiers)
    };

    Ok(HotKey::new(mods, code))
}

fn parse_key_code(s: &str) -> Result<Code, String> {
    // Single character → letter or digit
    if s.len() == 1 {
        let c = s.chars().next().unwrap();
        if c.is_ascii_alphabetic() {
            return letter_to_code(c.to_ascii_uppercase());
        }
        if c.is_ascii_digit() {
            return digit_to_code(c);
        }
    }

    // Named keys (case-insensitive)
    match s.to_lowercase().as_str() {
        // Function keys
        "f1" => Ok(Code::F1),
        "f2" => Ok(Code::F2),
        "f3" => Ok(Code::F3),
        "f4" => Ok(Code::F4),
        "f5" => Ok(Code::F5),
        "f6" => Ok(Code::F6),
        "f7" => Ok(Code::F7),
        "f8" => Ok(Code::F8),
        "f9" => Ok(Code::F9),
        "f10" => Ok(Code::F10),
        "f11" => Ok(Code::F11),
        "f12" => Ok(Code::F12),
        // Common named keys
        "space" => Ok(Code::Space),
        "enter" | "return" => Ok(Code::Enter),
        "tab" => Ok(Code::Tab),
        "escape" | "esc" => Ok(Code::Escape),
        "backspace" => Ok(Code::Backspace),
        "delete" | "del" => Ok(Code::Delete),
        "insert" | "ins" => Ok(Code::Insert),
        "home" => Ok(Code::Home),
        "end" => Ok(Code::End),
        "pageup" | "pgup" => Ok(Code::PageUp),
        "pagedown" | "pgdn" => Ok(Code::PageDown),
        // Arrow keys
        "up" | "arrowup" => Ok(Code::ArrowUp),
        "down" | "arrowdown" => Ok(Code::ArrowDown),
        "left" | "arrowleft" => Ok(Code::ArrowLeft),
        "right" | "arrowright" => Ok(Code::ArrowRight),
        // Punctuation
        "minus" | "-" => Ok(Code::Minus),
        "equal" | "equals" | "=" => Ok(Code::Equal),
        "bracketleft" | "[" => Ok(Code::BracketLeft),
        "bracketright" | "]" => Ok(Code::BracketRight),
        "backslash" | "\\" => Ok(Code::Backslash),
        "semicolon" | ";" => Ok(Code::Semicolon),
        "quote" | "'" => Ok(Code::Quote),
        "comma" | "," => Ok(Code::Comma),
        "period" | "." => Ok(Code::Period),
        "slash" | "/" => Ok(Code::Slash),
        "backquote" | "`" => Ok(Code::Backquote),
        _ => Err(format!("unknown key: '{s}'")),
    }
}

fn letter_to_code(c: char) -> Result<Code, String> {
    match c {
        'A' => Ok(Code::KeyA),
        'B' => Ok(Code::KeyB),
        'C' => Ok(Code::KeyC),
        'D' => Ok(Code::KeyD),
        'E' => Ok(Code::KeyE),
        'F' => Ok(Code::KeyF),
        'G' => Ok(Code::KeyG),
        'H' => Ok(Code::KeyH),
        'I' => Ok(Code::KeyI),
        'J' => Ok(Code::KeyJ),
        'K' => Ok(Code::KeyK),
        'L' => Ok(Code::KeyL),
        'M' => Ok(Code::KeyM),
        'N' => Ok(Code::KeyN),
        'O' => Ok(Code::KeyO),
        'P' => Ok(Code::KeyP),
        'Q' => Ok(Code::KeyQ),
        'R' => Ok(Code::KeyR),
        'S' => Ok(Code::KeyS),
        'T' => Ok(Code::KeyT),
        'U' => Ok(Code::KeyU),
        'V' => Ok(Code::KeyV),
        'W' => Ok(Code::KeyW),
        'X' => Ok(Code::KeyX),
        'Y' => Ok(Code::KeyY),
        'Z' => Ok(Code::KeyZ),
        _ => Err(format!("unknown letter key: '{c}'")),
    }
}

fn digit_to_code(c: char) -> Result<Code, String> {
    match c {
        '0' => Ok(Code::Digit0),
        '1' => Ok(Code::Digit1),
        '2' => Ok(Code::Digit2),
        '3' => Ok(Code::Digit3),
        '4' => Ok(Code::Digit4),
        '5' => Ok(Code::Digit5),
        '6' => Ok(Code::Digit6),
        '7' => Ok(Code::Digit7),
        '8' => Ok(Code::Digit8),
        '9' => Ok(Code::Digit9),
        _ => Err(format!("unknown digit key: '{c}'")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_default_hotkey() {
        let hk = parse_hotkey("Ctrl+Shift+P").unwrap();
        // Verify it has the expected modifiers and key
        let expected = HotKey::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::KeyP);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_single_modifier_and_key() {
        let hk = parse_hotkey("Alt+F1").unwrap();
        let expected = HotKey::new(Some(Modifiers::ALT), Code::F1);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_no_modifier() {
        let hk = parse_hotkey("F12").unwrap();
        let expected = HotKey::new(None, Code::F12);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_all_modifiers() {
        let hk = parse_hotkey("Ctrl+Shift+Alt+Super+A").unwrap();
        let expected = HotKey::new(
            Some(Modifiers::CONTROL | Modifiers::SHIFT | Modifiers::ALT | Modifiers::SUPER),
            Code::KeyA,
        );
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_case_insensitive_modifiers() {
        let hk1 = parse_hotkey("ctrl+shift+p").unwrap();
        let hk2 = parse_hotkey("CTRL+SHIFT+P").unwrap();
        let hk3 = parse_hotkey("Ctrl+Shift+P").unwrap();
        assert_eq!(hk1.id(), hk2.id());
        assert_eq!(hk2.id(), hk3.id());
    }

    #[test]
    fn parse_control_alias() {
        let hk1 = parse_hotkey("Ctrl+A").unwrap();
        let hk2 = parse_hotkey("Control+A").unwrap();
        assert_eq!(hk1.id(), hk2.id());
    }

    #[test]
    fn parse_win_meta_super_aliases() {
        let hk1 = parse_hotkey("Win+A").unwrap();
        let hk2 = parse_hotkey("Meta+A").unwrap();
        let hk3 = parse_hotkey("Super+A").unwrap();
        assert_eq!(hk1.id(), hk2.id());
        assert_eq!(hk2.id(), hk3.id());
    }

    #[test]
    fn parse_digit_keys() {
        let hk = parse_hotkey("Ctrl+1").unwrap();
        let expected = HotKey::new(Some(Modifiers::CONTROL), Code::Digit1);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_letter_case_insensitive() {
        let hk1 = parse_hotkey("Ctrl+p").unwrap();
        let hk2 = parse_hotkey("Ctrl+P").unwrap();
        assert_eq!(hk1.id(), hk2.id());
    }

    #[test]
    fn parse_named_keys() {
        assert!(parse_hotkey("Ctrl+Space").is_ok());
        assert!(parse_hotkey("Ctrl+Enter").is_ok());
        assert!(parse_hotkey("Ctrl+Tab").is_ok());
        assert!(parse_hotkey("Ctrl+Escape").is_ok());
        assert!(parse_hotkey("Ctrl+Delete").is_ok());
        assert!(parse_hotkey("Ctrl+Home").is_ok());
        assert!(parse_hotkey("Ctrl+End").is_ok());
        assert!(parse_hotkey("Ctrl+PageUp").is_ok());
        assert!(parse_hotkey("Ctrl+Up").is_ok());
    }

    #[test]
    fn parse_with_whitespace() {
        let hk = parse_hotkey("Ctrl + Shift + P").unwrap();
        let expected = HotKey::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::KeyP);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_empty_string_fails() {
        assert!(parse_hotkey("").is_err());
    }

    #[test]
    fn parse_only_modifiers_fails() {
        assert!(parse_hotkey("Ctrl+Shift").is_err());
    }

    #[test]
    fn parse_unknown_key_fails() {
        assert!(parse_hotkey("Ctrl+FooBar").is_err());
    }

    #[test]
    fn parse_multiple_keys_fails() {
        assert!(parse_hotkey("Ctrl+A+B").is_err());
    }

    #[test]
    fn invalid_hotkey_falls_back_to_default() {
        // parse_hotkey itself doesn't fall back — that's HotkeyManager's job.
        // But we verify the default always parses.
        let hk = parse_hotkey(DEFAULT_HOTKEY).unwrap();
        let expected = HotKey::new(Some(Modifiers::CONTROL | Modifiers::SHIFT), Code::KeyP);
        assert_eq!(hk.id(), expected.id());
    }

    #[test]
    fn parse_f_keys() {
        for i in 1..=12 {
            let s = format!("F{i}");
            assert!(parse_hotkey(&s).is_ok(), "failed to parse {s}");
        }
    }

    #[test]
    fn parse_all_letters() {
        for c in 'A'..='Z' {
            let s = format!("Ctrl+{c}");
            assert!(parse_hotkey(&s).is_ok(), "failed to parse Ctrl+{c}");
        }
    }

    #[test]
    fn parse_all_digits() {
        for d in '0'..='9' {
            let s = format!("Ctrl+{d}");
            assert!(parse_hotkey(&s).is_ok(), "failed to parse Ctrl+{d}");
        }
    }
}
