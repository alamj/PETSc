import configure

class Configure(configure.Configure):
  def __init__(self, framework):
    configure.Configure.__init__(self, framework)
    self.headerPrefix = ''
    self.substPrefix  = ''
    return

  def setOutput(self):
    #self.addDefine('HAVE_MATHEMATICA', 0)
    self.addSubstitution('MATHEMATICA_INCLUDE', '', 'The Mathematica include flags')
    self.addSubstitution('MATHEMATICA_LIB', '', 'The Mathematica library flags')
    return

  def configure(self):
    self.setOutput()
    return
