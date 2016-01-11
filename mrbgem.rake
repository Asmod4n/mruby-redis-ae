MRuby::Gem::Specification.new('mruby-redis-ae') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'mruby bindings for the redis event loop'
  spec.add_dependency 'mruby-errno'

  if build.toolchains.include?('android')
    spec.cc.flags << '-DHAVE_PTHREADS'
  else
    spec.linker.libraries << 'pthread'
  end
end
