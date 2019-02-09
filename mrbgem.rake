MRuby::Gem::Specification.new('mruby-require') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Internet Initiative Japan Inc.'

  ##spec.add_dependency 'mruby-array-ext'
  ##spec.add_dependency 'pins-mruby-dir'
  ##spec.add_dependency 'mruby-eval'
  ##spec.add_dependency 'pins-mruby-io'
  ##spec.add_test_dependency 'pins-mruby-tempfile'
  ##spec.add_test_dependency 'mruby-time'

  spec.cc.include_paths << "#{build.root}/src"
end
