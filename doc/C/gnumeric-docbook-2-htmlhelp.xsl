<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	        version="1.0">

<!--
	This file sets the parameters for the docbook to htmlhelp file
	transformation. For all the valid parameters see:
		http://docbook.sourceforge.net/release/xsl/snapshot/doc/html/
	or
		http://www.dpawson.co.uk/docbook/styling/param.xweb.html
	but that may be out of date.
	The parameters can be set here or can be added on the commandline
	as:
		xslproc dash,dash param name value
	and so on.
-->


  <xsl:import href="/usr/share/xml/docbook/stylesheet/nwalsh/htmlhelp/htmlhelp.xsl"/>

<!--
  <xsl:param name="" select=""/>
-->


  <!-- If one, adds admonishion graphics. -->
  <xsl:param name="admon.graphics" select="1"/>
  <!-- Sets the path for those graphics. -->
  <xsl:param name="admon.graphics.path" >figures/icons/</xsl:param>
  <!-- Sets the extension (and thereby the type) of the graphic. -->
  <xsl:param name="admon.graphics.extension" select="'.png'"/>
  <!-- If one, will generate an index. -->
  <xsl:param name="generate.index" select="1"/>

  <!--If one, adds forward/back arrows at top and bottom. -->
  <xsl:param name="suppress.navigation" select="1"/>

  <xsl:param name="htmlhelp.force.map.and.alias" select="1"/>

  <!-- The name of the .chm compiled help file -->
  <xsl:param name="htmlhelp.chm" select="'gnumeric.chm'"/>
  <!-- The name of the .hhp project file -->
  <xsl:param name="htmlhelp.hhp" select="'gnumeric.hhp'"/>
  <!-- The name of the .hhc table of contents file -->
  <xsl:param name="htmlhelp.hhc" select="'gnumeric.hhc'"/>
  <!-- The name of the .hhk index file -->
  <xsl:param name="htmlhelp.hhk" select="'gnumeric.hhk'"/>

  <!-- If zero, the ToC is not collapsed into a single root. -->
  <xsl:param name="htmlhelp.hhc.show.root" select="0"/>
  <!-- If one, the advanced search features will be available. -->
  <xsl:param name="htmlhelp.show.advanced.search" select="1"/>
  <!-- If one, the favorites tab will be shown. -->
  <xsl:param name="htmlhelp.show.favorities" select="1"/>

  <!-- Add an index using the .hhk file if non-zero -->
  <xsl:param name="htmlhelp.use.hhk" select="0"/>

  <!-- Sets the default topic on which to open the help viewer -->
  <xsl:param name="htmlhelp.default.topic" select="'ch01.html'"/>



</xsl:stylesheet>
