class RedisAe
  def run_once
    ret = process_events
    while (rc = process_events(ALL_EVENTS|DONT_WAIT)) > 0
      ret += rc
    end
    ret
  end
end
