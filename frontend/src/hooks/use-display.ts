import { useCallback, useEffect, useRef, useState } from "react"
import { backend, type DisplayState } from "@/lib/backend"

// The link state moves on its own, so the page re-reads rather than trusting what
// it fetched when it opened. Slow, because through the relay every poll takes that
// device's request gate.
const POLL_MS = 3000

/// Poll `display get`. A failed poll keeps the last known contents on screen.
export function useDisplay() {
  const [state, setState] = useState<DisplayState | null>(null)
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
      .getDisplay()
      .then((s) => {
        if (!alive.current) return
        setState(s)
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

  return { state, error }
}
