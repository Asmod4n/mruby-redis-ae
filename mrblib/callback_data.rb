class RedisAe
  class FileCallbackData
    attr_reader :sock, :mask, :block
  end
  class TimeCallbackData
    attr_reader :id, :finalizer, :block
  end
end
