import { useCallback, useEffect, useRef, useState } from "react"
import { backend, type Sensor } from "@/lib/backend"

// Sensors advertise every few seconds; a page refreshing slower than that still
// looks live, and through the relay every poll takes the device's request gate.
const POLL_MS = 5000

/// Poll `sensor list`. A failed poll keeps the last known list on screen.
export function useSensors() {
  const [sensors, setSensors] = useState<Sensor[] | null>(null)
  const [error, setError] = useState<string | null>(null)

  // A reply can land after unmount — the transport has no cancellation.
  const alive = useRef(true)
  useEffect(() => {
    alive.current = true
    return () => {
      alive.current = false
    }
  }, [])

  const refresh = useCallback(() => {
    backend
      .getSensors()
      .then((s) => {
        if (!alive.current) return
        setSensors(s)
        setError(null)
      })
      .catch((e) => {
        if (!alive.current) return
        setError(e instanceof Error ? e.message : String(e))
      })
  }, [])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, POLL_MS)
    return () => clearInterval(id)
  }, [refresh])

  return { sensors, error, refresh }
}
