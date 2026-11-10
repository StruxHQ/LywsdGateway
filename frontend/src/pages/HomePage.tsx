import { useState } from "react"
import {
  BatteryIcon,
  DropletsIcon,
  LockIcon,
  PencilIcon,
  ThermometerIcon,
} from "lucide-react"
import { toast } from "sonner"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { useDisplay } from "@/hooks/use-display"
import { useSensors } from "@/hooks/use-sensors"
import { backend, type Sensor } from "@/lib/backend"

// Where the custom firmware for a stock LYWSD03MMC comes from. Flashed once,
// from a browser with Web Bluetooth, and the sensor advertises in the clear.
const FLASHER_URL = "https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html"

/**
 * The home screen: every climate sensor the gateway hears over BLE
 * (main/app/SensorManager), and a mirror of the device's own display.
 */
export default function HomePage() {
  const { sensors, error, refresh } = useSensors()
  const [renaming, setRenaming] = useState<Sensor | null>(null)

  return (
    <div className="mx-auto max-w-4xl space-y-6">
      <div className="flex items-center gap-3">
        <ThermometerIcon className="text-muted-foreground size-6" />
        <h1 className="text-2xl font-bold">Sensors</h1>
      </div>

      {error && (
        <p className="text-destructive text-sm">
          Last poll failed: {error}. Showing the last known state.
        </p>
      )}

      {sensors === null ? (
        <p className="text-muted-foreground text-sm">Loading…</p>
      ) : sensors.length === 0 ? (
        <Card>
          <CardContent className="text-muted-foreground text-sm">
            No sensors heard yet. They advertise every few seconds, so one in range
            shows up within a minute.
          </CardContent>
        </Card>
      ) : (
        <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
          {sensors.map((s) => (
            <SensorCard key={s.mac} sensor={s} onRename={() => setRenaming(s)} />
          ))}
        </div>
      )}

      {sensors?.some((s) => s.encrypted) && (
        <p className="text-muted-foreground text-sm">
          Sensors marked <em>encrypted</em> run the stock Xiaomi firmware, whose
          readings need a per-device key. Flash them once with the{" "}
          <a className="underline" href={FLASHER_URL} target="_blank" rel="noreferrer">
            pvvx Telink flasher
          </a>{" "}
          and they show up here with values.
        </p>
      )}

      <DisplayMirror />

      <RenameDialog
        sensor={renaming}
        onClose={() => setRenaming(null)}
        onSaved={() => {
          setRenaming(null)
          refresh()
        }}
      />
    </div>
  )
}

function SensorCard({ sensor, onRename }: { sensor: Sensor; onRename: () => void }) {
  const stale = sensor.lastSeen > 300

  return (
    <Card className={stale ? "opacity-60" : undefined}>
      <CardHeader className="flex flex-row items-start justify-between gap-2">
        <div className="min-w-0">
          <CardTitle className="truncate">{sensor.name || "Unnamed"}</CardTitle>
          <p className="text-muted-foreground font-mono text-xs">{sensor.mac}</p>
        </div>
        <Button variant="ghost" size="icon" onClick={onRename} aria-label="Rename">
          <PencilIcon />
        </Button>
      </CardHeader>
      <CardContent className="space-y-3">
        {sensor.encrypted ? (
          <div className="text-muted-foreground flex items-center gap-2 text-sm">
            <LockIcon className="size-4" /> Encrypted — stock firmware
          </div>
        ) : (
          <div className="flex items-baseline gap-4">
            <span className="text-3xl font-semibold tabular-nums">
              {sensor.temperature !== undefined
                ? `${sensor.temperature.toFixed(1)} °C`
                : "—"}
            </span>
            {sensor.humidity !== undefined && (
              <span className="text-muted-foreground flex items-center gap-1 tabular-nums">
                <DropletsIcon className="size-4" />
                {sensor.humidity.toFixed(0)} %
              </span>
            )}
          </div>
        )}

        <div className="text-muted-foreground flex flex-wrap items-center gap-x-4 gap-y-1 text-xs">
          {sensor.battery !== undefined && (
            <span className="flex items-center gap-1">
              <BatteryIcon className="size-3.5" />
              {sensor.battery} %
              {sensor.voltage !== undefined && ` (${sensor.voltage.toFixed(2)} V)`}
            </span>
          )}
          <span>{sensor.rssi} dBm</span>
          <span>{describeAge(sensor.lastSeen)}</span>
          <Badge variant="outline">{sensor.format}</Badge>
        </div>
      </CardContent>
    </Card>
  )
}

function describeAge(seconds: number): string {
  if (seconds < 60) return `${seconds} s ago`
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`
  return `${Math.floor(seconds / 3600)} h ago`
}

function RenameDialog({
  sensor,
  onClose,
  onSaved,
}: {
  sensor: Sensor | null
  onClose: () => void
  onSaved: () => void
}) {
  const [name, setName] = useState("")
  const [busy, setBusy] = useState(false)

  // Reset the field to the sensor's current name each time the dialog opens.
  const [openFor, setOpenFor] = useState<string | null>(null)
  if (sensor && openFor !== sensor.mac) {
    setOpenFor(sensor.mac)
    setName(sensor.name)
  }
  if (!sensor && openFor !== null) setOpenFor(null)

  const save = () => {
    if (!sensor) return
    setBusy(true)
    backend
      .nameSensor(sensor.mac, name.trim())
      .then(onSaved)
      .catch((e) =>
        toast.error("Could not rename the sensor", {
          description: e instanceof Error ? e.message : String(e),
        }),
      )
      .finally(() => setBusy(false))
  }

  return (
    <Dialog open={sensor !== null} onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Name sensor</DialogTitle>
          <DialogDescription>
            {sensor?.mac}. The name shows on the display (first four letters) and as
            the <code>name</code> tag on its telemetry. Leave empty to remove it.
          </DialogDescription>
        </DialogHeader>
        <form
          onSubmit={(e) => {
            e.preventDefault()
            save()
          }}
          className="space-y-2"
        >
          <Label htmlFor="sensor-name">Name</Label>
          <Input
            id="sensor-name"
            value={name}
            maxLength={23}
            onChange={(e) => setName(e.target.value)}
            autoFocus
          />
        </form>
        <DialogFooter>
          <Button variant="outline" onClick={onClose}>
            Cancel
          </Button>
          <Button onClick={save} disabled={busy}>
            Save
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}

function DisplayMirror() {
  const { state } = useDisplay()

  return (
    <Card>
      <CardHeader>
        <CardTitle>Display</CardTitle>
      </CardHeader>
      <CardContent>
        {/* Always light-on-black, whatever the theme: it is a picture of an OLED. */}
        <pre
          className="w-fit rounded-md bg-black p-4 font-mono leading-snug text-sky-200"
          aria-label="Device display"
        >
          {state
            ? state.lines.map((line) => line.padEnd(state.columns)).join("\n")
            : "…"}
        </pre>
      </CardContent>
    </Card>
  )
}
