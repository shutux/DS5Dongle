import {
  AlertCircle,
  CheckCircle2,
  Download,
  Power,
  RefreshCw,
  RotateCcw,
  Save,
  Send,
  SlidersHorizontal,
  Usb,
} from "lucide-react";
import { ChangeEvent } from "react";
import { useDs5Bridge } from "./hooks/useDs5Bridge";
import {
  ConfigBody,
  ConfigValidationIssue,
  CONTROLLER_MODE_OPTIONS,
  ControllerMode,
  POLLING_RATE_OPTIONS,
  PollingRateMode,
  fieldIssue,
} from "./protocol/config";

export default function App() {
  const bridge = useDs5Bridge();
  const isBusy = bridge.operation !== null;
  const hasIssues = bridge.issues.length > 0;

  return (
    <main className="app-shell">
      <header className="app-header">
        <div>
          <div className="eyebrow">WebHID</div>
          <h1>DS5 Bridge Config</h1>
        </div>
        <div className={`status-pill ${bridge.isConnected ? "connected" : ""}`}>
          {bridge.isConnected ? <CheckCircle2 size={16} /> : <Usb size={16} />}
          <span>{bridge.statusText}</span>
        </div>
      </header>

      {bridge.error && (
        <div className="notice error" role="alert">
          <AlertCircle size={18} />
          <span>{bridge.error}</span>
          <button type="button" onClick={bridge.clearError}>
            Dismiss
          </button>
        </div>
      )}

      {!bridge.supported && (
        <div className="notice warning">
          <AlertCircle size={18} />
          <span>WebHID is available in Chromium-based browsers on secure origins.</span>
        </div>
      )}

      <section className="device-strip">
        <div className="device-main">
          <div className="device-icon">
            <Usb size={22} />
          </div>
          <div>
            <div className="label">Device</div>
            <strong>{bridge.deviceLabel}</strong>
          </div>
        </div>
        <div className="device-actions">
          {bridge.authorizedDevices.length > 0 && !bridge.client && (
            <button
              type="button"
              className="button secondary"
              onClick={() => bridge.connectAuthorized(bridge.authorizedDevices[0])}
              disabled={isBusy}
              title="Open the first previously authorized device"
            >
              <Power size={17} />
              Open
            </button>
          )}
          <button
            type="button"
            className="button primary"
            onClick={bridge.connect}
            disabled={!bridge.supported || isBusy}
            title="Choose a DS5 Bridge HID device"
          >
            <Usb size={17} />
            Connect
          </button>
        </div>
      </section>

      <div className="content-grid">
        <section className="panel config-panel">
          <div className="panel-title">
            <SlidersHorizontal size={18} />
            <h2>Configuration</h2>
          </div>

          <div className="control-stack">
            <FloatControl
              label="Haptics gain"
              value={bridge.draft.hapticsGain}
              min={1}
              max={2}
              step={0.05}
              issue={fieldIssue(bridge.issues, "hapticsGain")}
              onChange={(value) => bridge.setDraftField("hapticsGain", value)}
            />
            <FloatControl
              label="Speaker volume"
              value={bridge.draft.speakerVolume}
              min={1}
              max={2}
              step={0.05}
              issue={fieldIssue(bridge.issues, "speakerVolume")}
              onChange={(value) => bridge.setDraftField("speakerVolume", value)}
            />
            <ToggleControl
              label="Disable inactive disconnect"
              value={bridge.draft.disableInactiveDisconnect}
              onChange={(value) => bridge.setDraftField("disableInactiveDisconnect", value)}
            />
            <ToggleControl
              label="Disable Pico LED"
              value={bridge.draft.disablePicoLed}
              onChange={(value) => bridge.setDraftField("disablePicoLed", value)}
            />
            <PollingRateControl
              value={bridge.draft.pollingRateMode}
              onChange={(value) => bridge.setDraftField("pollingRateMode", value)}
            />
            <IntegerControl
              label="Haptics buffer length"
              value={bridge.draft.hapticsBufferLength}
              min={16}
              max={255}
              issue={fieldIssue(bridge.issues, "hapticsBufferLength")}
              onChange={(value) => bridge.setDraftField("hapticsBufferLength", value)}
            />
            <ControllerModeControl
              value={bridge.draft.controllerMode}
              onChange={(value) => bridge.setDraftField("controllerMode", value)}
            />
          </div>
        </section>

        <aside className="panel side-panel">
          <div className="panel-title">
            <Download size={18} />
            <h2>Actions</h2>
          </div>

          <div className="action-stack">
            <button
              type="button"
              className="button secondary wide"
              onClick={bridge.readConfig}
              disabled={!bridge.client || isBusy}
              title="Read current config from report 0xF7"
            >
              <RefreshCw size={17} />
              Read
            </button>
            <button
              type="button"
              className="button primary wide"
              onClick={bridge.applyConfig}
              disabled={!bridge.client || isBusy || !bridge.isDirty || hasIssues}
              title="Send command 0x01 through report 0xF6"
            >
              <Send size={17} />
              Apply to Device
            </button>
            <button
              type="button"
              className="button success wide"
              onClick={bridge.saveToFlash}
              disabled={!bridge.client || isBusy || bridge.isDirty}
              title={bridge.isDirty ? "Apply changes before saving" : "Send command 0x02 through report 0xF6"}
            >
              <Save size={17} />
              Save to Flash
            </button>
            <button
              type="button"
              className="button secondary wide"
              onClick={bridge.reconnectUsb}
              disabled={!bridge.client || isBusy}
              title="Send command 0x03 through report 0xF6"
            >
              <Power size={17} />
              Reconnect USB
            </button>
            <button
              type="button"
              className="button ghost wide"
              onClick={bridge.resetDraft}
              disabled={!bridge.config || isBusy || !bridge.isDirty}
              title="Restore the last config read or applied"
            >
              <RotateCcw size={17} />
              Reset Edits
            </button>
          </div>

          <div className="state-box">
            <div className="label">State</div>
            <strong>{bridge.statusText}</strong>
            {bridge.issues.length > 0 && (
              <ul>
                {bridge.issues.map((issue) => (
                  <li key={issue.field}>{issue.message}</li>
                ))}
              </ul>
            )}
          </div>
        </aside>
      </div>
    </main>
  );
}

interface FloatControlProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  issue?: ConfigValidationIssue;
  onChange: (value: number) => void;
}

function FloatControl({ label, value, min, max, step, issue, onChange }: FloatControlProps) {
  const handleChange = (event: ChangeEvent<HTMLInputElement>) => {
    const next = Number(event.currentTarget.value);
    if (Number.isFinite(next)) {
      onChange(next);
    }
  };

  return (
    <label className={`control-row ${issue ? "invalid" : ""}`}>
      <span>
        <strong>{label}</strong>
        {issue && <small>{issue.message}</small>}
      </span>
      <div className="range-inputs">
        <input type="range" min={min} max={max} step={step} value={value} onChange={handleChange} />
        <input type="number" min={min} max={max} step={step} value={value.toFixed(2)} onChange={handleChange} />
      </div>
    </label>
  );
}

interface IntegerControlProps {
  label: string;
  value: number;
  min: number;
  max: number;
  issue?: ConfigValidationIssue;
  onChange: (value: number) => void;
}

function IntegerControl({ label, value, min, max, issue, onChange }: IntegerControlProps) {
  const handleChange = (event: ChangeEvent<HTMLInputElement>) => {
    const next = Number(event.currentTarget.value);
    if (Number.isFinite(next)) {
      onChange(Math.round(next));
    }
  };

  return (
    <label className={`control-row ${issue ? "invalid" : ""}`}>
      <span>
        <strong>{label}</strong>
        {issue && <small>{issue.message}</small>}
      </span>
      <div className="range-inputs">
        <input type="range" min={min} max={max} step={1} value={value} onChange={handleChange} />
        <input type="number" min={min} max={max} step={1} value={value} onChange={handleChange} />
      </div>
    </label>
  );
}

interface ToggleControlProps {
  label: keyof Pick<ConfigBody, "disableInactiveDisconnect" | "disablePicoLed"> | string;
  value: boolean;
  onChange: (value: boolean) => void;
}

function ToggleControl({ label, value, onChange }: ToggleControlProps) {
  return (
    <div className="control-row toggle-row">
      <strong>{label}</strong>
      <button
        type="button"
        className={`switch ${value ? "on" : ""}`}
        role="switch"
        aria-checked={value}
        onClick={() => onChange(!value)}
        title={value ? "Enabled" : "Disabled"}
      >
        <span />
      </button>
    </div>
  );
}

interface PollingRateControlProps {
  value: PollingRateMode;
  onChange: (value: PollingRateMode) => void;
}

function PollingRateControl({ value, onChange }: PollingRateControlProps) {
  return (
    <div className="control-row">
      <strong>Polling rate mode</strong>
      <div className="segmented-control" role="group" aria-label="Polling rate mode">
        {POLLING_RATE_OPTIONS.map((option) => (
          <button
            type="button"
            key={option.value}
            className={option.value === value ? "selected" : ""}
            onClick={() => onChange(option.value)}
            aria-pressed={option.value === value}
          >
            {option.label}
          </button>
        ))}
      </div>
    </div>
  );
}

interface ControllerModeControlProps {
  value: ControllerMode;
  onChange: (value: ControllerMode) => void;
}

function ControllerModeControl({ value, onChange }: ControllerModeControlProps) {
  return (
    <div className="control-row">
      <strong>Controller mode</strong>
      <div className="segmented-control two-options" role="group" aria-label="Controller mode">
        {CONTROLLER_MODE_OPTIONS.map((option) => (
          <button
            type="button"
            key={option.value}
            className={option.value === value ? "selected" : ""}
            onClick={() => onChange(option.value)}
            aria-pressed={option.value === value}
          >
            {option.label}
          </button>
        ))}
      </div>
    </div>
  );
}
